# Song Editor - Standalone Version

The Song Editor is now a standalone program that can be used independently of the main VERSE game. It provides a complete music composition and editing environment.

## Features

- **Piano Roll Editor**: Visual note editing with a piano roll interface
- **Multi-track Support**: Create and edit multiple instrument tracks
- **Real-time Playback**: Play, pause, and stop your compositions
- **Grid-based Editing**: Snap notes to a musical grid for precise timing
- **Note Selection**: Click to select and edit individual notes
- **MIDI-like Interface**: Familiar interface for musicians

## Building

To build the standalone song editor:

```bash
make song-editor
```

This will create a `song-editor` executable.

## Running

To run the song editor:

```bash
./song-editor
```

## Controls

### Mouse Controls
- **Left Click**: Add notes to the piano roll
- **Right Click**: Remove notes from the piano roll
- **Click and Drag**: Select multiple notes

### Keyboard Controls
- **Space**: Play/Pause the current song
- **S**: Stop playback
- **Delete**: Remove selected notes
- **ESC**: Exit the program

### Toolbar Buttons
- **Play/Pause**: Control playback
- **Select**: Selection mode for editing notes
- **Draw**: Drawing mode for adding notes
- **Erase**: Erase mode for removing notes
- **Grid**: Toggle grid display

## File Format

The song editor uses a custom JSON-based format for saving and loading songs. Songs are saved with the following structure:

```json
{
  "name": "Song Name",
  "artist": "Composer Name",
  "tempo": 120,
  "tracks": [
    {
      "name": "Track Name",
      "channel": 0,
      "events": [
        {
          "note": 60,
          "velocity": 100,
          "start_time": 0,
          "duration": 480
        }
      ]
    }
  ]
}
```

## Architecture

The standalone song editor consists of several components:

- **song_editor_standalone.c**: Main application entry point and window management
- **song_editor.c**: Core editor functionality and UI rendering
- **songwriter/songwriter.c**: Music composition and playback engine
- **synthesizer/synthesizer.c**: Audio synthesis and generation
- **sequencer/melody_loader.c**: File I/O for song loading/saving

## Integration with Main Game

The song editor has been completely removed from the main VERSE game. The main game no longer includes:

- Song editor button in the main menu
- Song editor screen and state management
- Song editor dependencies in the build system

This separation allows the song editor to be developed and used independently while keeping the main game focused on its core gameplay features.

## Development

The song editor uses conditional compilation to support both standalone and integrated modes:

- `SONG_EDITOR_STANDALONE` flag enables standalone mode
- Uses a simplified window system for standalone operation
- Maintains compatibility with the main game's window system when integrated

## Dependencies

- SDL2 (for window management and audio)
- SDL2_ttf (for text rendering)
- Standard C library

## Future Enhancements

Potential improvements for the standalone song editor:

- File save/load functionality
- More instrument sounds
- Effects and filters
- MIDI import/export
- Undo/redo functionality
- Keyboard shortcuts
- Custom themes
- Plugin system for additional instruments
