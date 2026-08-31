# MIDI File Reader

This module provides functionality to read MIDI files and convert them to the existing song format used in the VERSE engine.

## Features

- **MIDI File Parsing**: Reads standard MIDI files (Format 0, 1, and 2)
- **Track Conversion**: Converts MIDI tracks to the internal song format
- **Tempo Support**: Extracts and converts tempo information
- **Note Events**: Converts MIDI note on/off events to song notes
- **Error Handling**: Comprehensive error reporting for malformed files

## Files

- `src/midi_reader.h` - Header file with MIDI reader API
- `src/midi_reader.c` - Implementation of MIDI file parsing and conversion
- `test_midi_reader.c` - Test program demonstrating usage

## Usage

### Basic Usage

```c
#include "src/midi_reader.h"

// Create MIDI reader
MIDIFile* midi = midi_reader_create();
if (!midi) {
    printf("Failed to create MIDI reader\n");
    return 1;
}

// Load MIDI file
if (!midi_reader_load_file(midi, "song.mid")) {
    MIDIError error = midi_reader_get_last_error();
    printf("Error: %s\n", midi_reader_get_error_string(error));
    midi_reader_destroy(midi);
    return 1;
}

// Print file information
midi_reader_print_info(midi);

// Convert to song format
Song* song = midi_reader_convert_to_song(midi);
if (song) {
    // Use the song with the songwriter system
    Songwriter* writer = songwriter_create(44100);
    songwriter_load_song(writer, song);

    // Generate audio or perform other operations
    // ...

    songwriter_destroy(writer);
    songwriter_destroy_song(song);
}

// Cleanup
midi_reader_destroy(midi);
```

### Test Program

Build and run the test program:

```bash
make test-midi-reader
./test-midi-reader assets/tracks/Goldeneye_64_-_James_Bond_Theme.mid
```

## MIDI File Format Support

The reader supports:

- **Format 0**: Single track with all channels
- **Format 1**: Multiple tracks (one track per channel)
- **Format 2**: Multiple tracks (independent tracks)

### Supported MIDI Events

- **Note On/Off**: Converts to song notes with duration
- **Tempo Changes**: Extracts tempo information
- **Track Names**: Preserves track names
- **Time Signatures**: Reads time signature information
- **Program Changes**: Tracks instrument changes
- **Control Changes**: Handles volume, pan, and other controllers

### Conversion Details

1. **Tempo Conversion**: MIDI tempo (microseconds per quarter note) is converted to BPM
2. **Note Duration**: Note on/off pairs are matched to calculate note durations
3. **Velocity**: MIDI velocity (0-127) is preserved as note velocity
4. **Track Structure**: Each MIDI track becomes a song track
5. **Timing**: MIDI ticks are preserved in the song format

## Error Handling

The module provides comprehensive error reporting:

```c
typedef enum {
    MIDI_ERROR_NONE = 0,
    MIDI_ERROR_FILE_NOT_FOUND,
    MIDI_ERROR_INVALID_HEADER,
    MIDI_ERROR_INVALID_TRACK,
    MIDI_ERROR_UNSUPPORTED_FORMAT,
    MIDI_ERROR_MEMORY_ALLOCATION,
    MIDI_ERROR_CORRUPTED_DATA
} MIDIError;
```

## Integration with Existing Systems

The MIDI reader integrates seamlessly with:

- **Songwriter System**: Converted songs can be used with the existing songwriter
- **Synthesizer**: Generated songs can be played through the synthesizer
- **Melody System**: MIDI tracks can be converted to melody format
- **Background Music**: Converted songs can be used as background music

## Example Output

When running the test program on a MIDI file:

```
Loading MIDI file: assets/tracks/Goldeneye_64_-_James_Bond_Theme.mid

=== MIDI File Information ===
MIDI File: assets/tracks/Goldeneye_64_-_James_Bond_Theme.mid
Format: 1
Tracks: 16
Division: 480 ticks per quarter note
Tempo: 120 BPM
Total ticks: 38400
Duration: 32.00 seconds
Track 0: Tempo Track (3 events)
Track 1: Bass (45 events)
Track 2: Drums (127 events)
...

=== Converting to Song Format ===

=== Converted Song Information ===
Song: Converted MIDI
Artist: MIDI Import
Tempo: 120 BPM
Tracks: 16
Total ticks: 38400

=== Testing Playback ===
Song loaded successfully for playback.
Song duration: 32.00 seconds
Generating 5 seconds of audio...
Audio generation completed.

=== Conversion Complete ===
The MIDI file has been successfully converted to the song format.
You can now use this song data in your application.
```

## Building

The MIDI reader is integrated into the main Makefile. To build:

```bash
# Build just the MIDI reader test
make test-midi-reader

# Build all tests including MIDI reader
make test

# Build everything
make all
```

## Dependencies

The MIDI reader depends on:
- `src/songwriter.h` - Song format definitions
- `src/synthesizer/synthesizer.h` - Audio synthesis
- Standard C library functions

## Limitations

- Currently focuses on note events (melody/harmony)
- Limited support for complex MIDI controllers
- No support for MIDI system exclusive messages
- Tempo changes within tracks are not fully supported

## Future Enhancements

- Support for MIDI system exclusive messages
- Better handling of tempo changes within tracks
- Support for MIDI controller data
- Export back to MIDI format
- Real-time MIDI input support
