#ifndef MIDI_READER_H
#define MIDI_READER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "songwriter/songwriter.h"

// MIDI file format constants
#define MIDI_HEADER_MAGIC 0x4D546864  // "MThd"
#define MIDI_TRACK_MAGIC  0x4D54726B  // "MTrk"

// MIDI message types
#define MIDI_NOTE_OFF          0x80
#define MIDI_NOTE_ON           0x90
#define MIDI_POLY_PRESSURE     0xA0
#define MIDI_CONTROL_CHANGE    0xB0
#define MIDI_PROGRAM_CHANGE    0xC0
#define MIDI_CHANNEL_PRESSURE 0xD0
#define MIDI_PITCH_BEND       0xE0
#define MIDI_SYSEX            0xF0
#define MIDI_META             0xFF

// MIDI meta events
#define MIDI_META_SEQUENCE_NUMBER 0x00
#define MIDI_META_TEXT            0x01
#define MIDI_META_COPYRIGHT       0x02
#define MIDI_META_TRACK_NAME      0x03
#define MIDI_META_INSTRUMENT_NAME 0x04
#define MIDI_META_LYRIC           0x05
#define MIDI_META_MARKER          0x06
#define MIDI_META_CUE_POINT       0x07
#define MIDI_META_CHANNEL_PREFIX  0x20
#define MIDI_META_END_OF_TRACK    0x2F
#define MIDI_META_TEMPO           0x51
#define MIDI_META_SMPTE_OFFSET    0x54
#define MIDI_META_TIME_SIGNATURE  0x58
#define MIDI_META_KEY_SIGNATURE   0x59
#define MIDI_META_SEQUENCER_SPECIFIC 0x7F

// MIDI file header structure
typedef struct {
    uint32_t magic;        // "MThd"
    uint32_t header_length; // Always 6
    uint16_t format;       // 0, 1, or 2
    uint16_t num_tracks;   // Number of tracks
    uint16_t division;     // Ticks per quarter note
} MIDIHeader;

// MIDI track header structure
typedef struct {
    uint32_t magic;        // "MTrk"
    uint32_t track_length; // Length of track data
} MIDITrackHeader;

// MIDI event structure
typedef struct {
    uint32_t delta_time;   // Delta time in ticks
    uint8_t status;        // Status byte
    uint8_t data1;         // First data byte
    uint8_t data2;         // Second data byte (if applicable)
    uint32_t absolute_time; // Absolute time in ticks
} MIDIEvent;

// MIDI track structure
typedef struct {
    char name[64];
    MIDIEvent* events;
    uint32_t event_count;
    uint32_t event_capacity;
    uint32_t track_length;
} MIDITrack;

// MIDI file structure
typedef struct {
    char filename[256];
    MIDIHeader header;
    MIDITrack* tracks;
    uint16_t num_tracks;
    uint32_t total_ticks;
    uint32_t tempo;        // Microseconds per quarter note
} MIDIFile;

// MIDI reader functions
MIDIFile* midi_reader_create(void);
void midi_reader_destroy(MIDIFile* midi);

// File loading
bool midi_reader_load_file(MIDIFile* midi, const char* filename);
bool midi_reader_parse_header(MIDIFile* midi, FILE* file);
bool midi_reader_parse_track(MIDIFile* midi, FILE* file, uint16_t track_index);

// Event parsing
uint32_t midi_reader_read_variable_length(FILE* file);
bool midi_reader_parse_event(MIDIFile* midi, MIDITrack* track, FILE* file, uint32_t* current_time);

// Conversion to song format
Song* midi_reader_convert_to_song(MIDIFile* midi);
Track* midi_reader_convert_track_to_song_track(MIDIFile* midi, MIDITrack* midi_track, uint8_t channel);

// Utility functions
uint32_t midi_reader_get_tempo(MIDIFile* midi);
void midi_reader_print_info(MIDIFile* midi);
bool midi_reader_is_valid_file(const char* filename);

// Error handling
typedef enum {
    MIDI_ERROR_NONE = 0,
    MIDI_ERROR_FILE_NOT_FOUND,
    MIDI_ERROR_INVALID_HEADER,
    MIDI_ERROR_INVALID_TRACK,
    MIDI_ERROR_UNSUPPORTED_FORMAT,
    MIDI_ERROR_MEMORY_ALLOCATION,
    MIDI_ERROR_CORRUPTED_DATA
} MIDIError;

MIDIError midi_reader_get_last_error(void);
const char* midi_reader_get_error_string(MIDIError error);

// Export a Song to the JSON melody format
bool midi_reader_export_song_to_json(const Song* song, const char* filename, const char* melody_name, const char* description);

#endif // MIDI_READER_H
