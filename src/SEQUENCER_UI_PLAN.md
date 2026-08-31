# 🎵 Sequencer UI Development Plan

## Overview

The existing `songwriter` system provides an excellent modular foundation for building a full sequencer UI. The architecture is already well-designed with:

- **Comprehensive API**: 280+ lines of well-structured functions
- **Advanced Features**: MIDI support, polyphony, ADSR, filters, effects
- **Audio Quality**: High-resolution timing, normalization, compression
- **Modular Design**: Clean separation between song, track, and note management

## 🎯 Sequencer UI Architecture

### Core UI Modules

```
sequencer_ui/
├── sequencer_ui_core.h/c          # Main UI controller
├── sequencer_timeline.h/c         # Timeline/piano roll view
├── sequencer_track_list.h/c       # Track management panel
├── sequencer_mixer.h/c            # Mixer/effects panel
├── sequencer_transport.h/c        # Play/stop/record controls
├── sequencer_piano_roll.h/c       # Piano roll editor
├── sequencer_pattern_editor.h/c   # Pattern/step sequencer
├── sequencer_instrument_panel.h/c # Instrument/synth controls
└── sequencer_ui_theme.h/c         # UI styling and themes
```

### Integration with Existing Systems

```
verse_client_modular.c
├── client_audio.c (existing)
│   └── songwriter integration
├── client_render.c (existing)
│   └── sequencer UI rendering
└── sequencer_ui_core.c (new)
    ├── Timeline view
    ├── Track list
    ├── Mixer panel
    └── Transport controls
```

## 🎨 UI Components Design

### 1. **Main Sequencer Window**
```
┌─────────────────────────────────────────────────────────┐
│ Transport Controls    [▶] [⏸] [⏹] [⏺] [⏮] [⏭] [🔄]    │
├─────────────────────────────────────────────────────────┤
│ Track List │ Piano Roll / Timeline View                │
│ ┌─────────┐ │ ┌─────────────────────────────────────────┐ │
│ │ Track 1 │ │ │ C4  ████                               │ │
│ │ Track 2 │ │ │ C#4     ██                             │ │
│ │ Track 3 │ │ │ D4      ████                           │ │
│ │ Track 4 │ │ │ D#4         ██                         │ │
│ └─────────┘ │ │ E4            ████                     │ │
│             │ │ F4                ██                   │ │
│             │ │ F#4                   ████             │ │
│             │ │ G4                       ██           │ │
│             │ │ G#4                          ████     │ │
│             │ │ A4                              ██   │ │
│             │ │ A#4                                 ██│ │
│             │ │ B4                                   │ │
│             │ └─────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────┤
│ Mixer Panel                                            │
│ Track 1 [Vol] [Pan] [Mute] [Solo] [FX] [EQ]           │
│ Track 2 [Vol] [Pan] [Mute] [Solo] [FX] [EQ]           │
│ Track 3 [Vol] [Pan] [Mute] [Solo] [FX] [EQ]           │
│ Master  [Vol] [FX] [EQ] [Limiter]                     │
└─────────────────────────────────────────────────────────┘
```

### 2. **Piano Roll Editor**
- **Vertical**: Piano keys (C0-C8)
- **Horizontal**: Timeline with beat/measure markers
- **Note Blocks**: Draggable, resizable note rectangles
- **Velocity**: Color-coded or height-based velocity display
- **Zoom**: Horizontal and vertical zoom controls
- **Grid**: Snap to beat, 1/4, 1/8, 1/16 note divisions

### 3. **Track Management**
- **Track List**: Name, instrument, volume, mute, solo
- **Track Properties**: Channel, program, effects chain
- **Track Creation**: New track with instrument selection
- **Track Deletion**: With confirmation dialog

### 4. **Mixer Panel**
- **Per-Track Controls**: Volume, pan, mute, solo
- **Effects Chain**: Reverb, delay, filter, compressor
- **EQ**: 3-band EQ per track
- **Master Controls**: Master volume, limiter, global effects

### 5. **Transport Controls**
- **Playback**: Play, pause, stop, record
- **Navigation**: Previous/next measure, loop points
- **Tempo**: BPM control with tap tempo
- **Time Signature**: Numerator/denominator controls
- **Loop**: Loop region selection and toggle

## 🛠️ Implementation Plan

### Phase 1: Core UI Framework
```c
// sequencer_ui_core.h
typedef struct {
    Songwriter* songwriter;
    SDL_Window* window;
    SDL_Renderer* renderer;

    // UI State
    bool is_open;
    int window_width, window_height;

    // UI Components
    SequencerTimeline* timeline;
    SequencerTrackList* track_list;
    SequencerMixer* mixer;
    SequencerTransport* transport;

    // Interaction State
    int selected_track;
    uint32_t selected_note;
    bool is_dragging;
    int drag_start_x, drag_start_y;
} SequencerUI;
```

### Phase 2: Timeline View
```c
// sequencer_timeline.h
typedef struct {
    // Timeline state
    uint32_t current_tick;
    uint32_t visible_start_tick;
    uint32_t visible_end_tick;
    float pixels_per_tick;
    float pixels_per_note;

    // Piano roll
    int visible_notes_start;  // C0 = 0, C1 = 12, etc.
    int visible_notes_count;

    // Interaction
    bool is_playing;
    uint32_t loop_start, loop_end;
} SequencerTimeline;
```

### Phase 3: Track Management
```c
// sequencer_track_list.h
typedef struct {
    Track** tracks;
    int track_count;
    int selected_track;
    int track_height;

    // Track properties
    bool show_volume;
    bool show_pan;
    bool show_mute_solo;
} SequencerTrackList;
```

### Phase 4: Mixer Integration
```c
// sequencer_mixer.h
typedef struct {
    // Per-track controls
    float* track_volumes;
    float* track_pans;
    bool* track_muted;
    bool* track_soloed;

    // Master controls
    float master_volume;
    bool master_limiter_enabled;

    // Effects
    EffectChain** track_effects;
    EffectChain* master_effects;
} SequencerMixer;
```

## 🎵 Advanced Features

### 1. **Pattern Editor**
- Step sequencer for drum patterns
- 16-step grid with velocity per step
- Pattern chaining and variations
- Swing and groove templates

### 2. **Instrument Panel**
- Real-time synth parameter control
- ADSR envelope visualization
- Filter cutoff/resonance knobs
- LFO controls with waveform display

### 3. **MIDI Integration**
- MIDI input for live recording
- MIDI output for external hardware
- MIDI file import/export
- MIDI controller mapping

### 4. **Audio Effects**
- Built-in effects: reverb, delay, chorus, distortion
- Effect chain per track
- Real-time parameter automation
- Effect presets and libraries

## 🔧 Integration with Verse Client

### 1. **Menu Integration**
```c
// Add to client_render.c
void render_sequencer_menu(void) {
    if (show_sequencer) {
        sequencer_ui_render();
    }
}
```

### 2. **Audio Integration**
```c
// Add to client_audio.c
void client_audio_update_sequencer(double delta_time) {
    if (sequencer_ui_is_open()) {
        sequencer_ui_update_audio(delta_time);
    }
}
```

### 3. **Input Integration**
```c
// Add to client_input.c
void handle_sequencer_input(int key) {
    if (sequencer_ui_is_open()) {
        sequencer_ui_handle_input(key);
    }
}
```

## 📁 File Structure

```
src/
├── sequencer_ui/
│   ├── sequencer_ui_core.h/c
│   ├── sequencer_timeline.h/c
│   ├── sequencer_track_list.h/c
│   ├── sequencer_mixer.h/c
│   ├── sequencer_transport.h/c
│   ├── sequencer_piano_roll.h/c
│   ├── sequencer_pattern_editor.h/c
│   ├── sequencer_instrument_panel.h/c
│   ├── sequencer_ui_theme.h/c
│   └── sequencer_ui_demo.c
├── songwriter/ (existing)
│   ├── songwriter.h/c
│   └── synthesizer/ (existing)
└── client_*.c (existing - add sequencer integration)
```

## 🎯 Development Priorities

### High Priority
1. **Core UI Framework** - Basic window and component system
2. **Timeline View** - Piano roll with note editing
3. **Transport Controls** - Play/stop/record functionality
4. **Track Management** - Basic track list and properties

### Medium Priority
1. **Mixer Panel** - Volume, pan, mute, solo controls
2. **Pattern Editor** - Step sequencer for drums
3. **MIDI Integration** - Input/output support
4. **Audio Effects** - Basic effects chain

### Low Priority
1. **Advanced Features** - Automation, advanced effects
2. **Themes** - Customizable UI appearance
3. **Plugins** - VST/AU plugin support
4. **Collaboration** - Multi-user editing

## 🚀 Getting Started

The songwriter system is already production-ready with:
- ✅ **280+ API functions** for complete sequencer functionality
- ✅ **Advanced audio features** (polyphony, effects, high-resolution timing)
- ✅ **MIDI compatibility** with full note/velocity/channel support
- ✅ **Modular architecture** ready for UI integration

**Next Step**: Create `sequencer_ui_core.c` to build the first UI component on top of this solid foundation!

---

This plan leverages the excellent modular design of the existing songwriter system to create a professional-grade sequencer UI that integrates seamlessly with the verse client architecture.
