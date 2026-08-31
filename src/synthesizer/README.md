# VERSE Synthesizer Module

A real-time software synthesizer implementation in C, featuring multiple wave types, multi-channel support, and ADSR envelope control.

## Features

### Wave Types
- **Sine Wave**: Pure, smooth tone with fundamental frequency only
- **Saw Wave**: Rich harmonic content, bright and buzzy
- **Square Wave**: Hollow, electronic sound with odd harmonics
- **Triangle Wave**: Mellow, flute-like tone with soft harmonics

### Multi-Channel Support
- Up to 16 simultaneous channels
- Independent frequency, amplitude, and wave type per channel
- Real-time channel management (add/remove/modify)

### ADSR Envelope
- **Attack**: Time to reach full volume
- **Decay**: Time to reach sustain level
- **Sustain**: Level held while note is active
- **Release**: Time to fade to silence after note off

### Real-Time Control
- Note on/off functionality
- Frequency modulation
- Amplitude control
- Master volume control
- Wave type switching

## API Reference

### Core Functions

```c
// Create and destroy synthesizer
Synthesizer* synthesizer_create(int sample_rate);
void synthesizer_destroy(Synthesizer* synth);

// Generate audio
float synthesizer_generate_sample(Synthesizer* synth);
void synthesizer_generate_buffer(Synthesizer* synth, float* buffer, int num_samples);
```

### Channel Management

```c
// Add/remove channels
int synthesizer_add_channel(Synthesizer* synth, WaveType wave_type, float frequency, float amplitude);
void synthesizer_remove_channel(Synthesizer* synth, int channel_id);

// Modify channel parameters
void synthesizer_set_channel_frequency(Synthesizer* synth, int channel_id, float frequency);
void synthesizer_set_channel_amplitude(Synthesizer* synth, int channel_id, float amplitude);
void synthesizer_set_channel_wave_type(Synthesizer* synth, int channel_id, WaveType wave_type);
```

### ADSR Envelope

```c
// Set ADSR parameters
void synthesizer_set_channel_adsr(Synthesizer* synth, int channel_id,
                                  float attack, float decay, float sustain, float release);

// Note control
void synthesizer_note_on(Synthesizer* synth, int channel_id);
void synthesizer_note_off(Synthesizer* synth, int channel_id);
```

### Utility Functions

```c
// Volume control
void synthesizer_set_master_volume(Synthesizer* synth, float volume);

// Status queries
int synthesizer_get_active_channels(Synthesizer* synth);
bool synthesizer_is_channel_active(Synthesizer* synth, int channel_id);
```

## Usage Examples

### Basic Setup

```c
#include "synthesizer.h"

// Create synthesizer at 44.1kHz
Synthesizer* synth = synthesizer_create(44100);

// Add a sine wave channel
int channel = synthesizer_add_channel(synth, WAVE_SINE, 440.0f, 0.5f);

// Set ADSR envelope
synthesizer_set_channel_adsr(synth, channel, 0.1f, 0.1f, 0.8f, 0.3f);

// Play a note
synthesizer_note_on(synth, channel);

// Generate audio samples
for (int i = 0; i < 44100; i++) {
    float sample = synthesizer_generate_sample(synth);
    // Process sample (e.g., send to audio output)
}

// Stop the note
synthesizer_note_off(synth, channel);

// Cleanup
synthesizer_destroy(synth);
```

### Multi-Channel Chord

```c
// Create a C major chord (C-E-G)
int c_channel = synthesizer_add_channel(synth, WAVE_SINE, 261.63f, 0.3f);
int e_channel = synthesizer_add_channel(synth, WAVE_SINE, 329.63f, 0.3f);
int g_channel = synthesizer_add_channel(synth, WAVE_SINE, 392.00f, 0.3f);

// Play all notes together
synthesizer_note_on(synth, c_channel);
synthesizer_note_on(synth, e_channel);
synthesizer_note_on(synth, g_channel);
```

### Wave Type Comparison

```c
// Create channels with different wave types at same frequency
int sine_ch = synthesizer_add_channel(synth, WAVE_SINE, 440.0f, 0.25f);
int saw_ch = synthesizer_add_channel(synth, WAVE_SAW, 440.0f, 0.25f);
int square_ch = synthesizer_add_channel(synth, WAVE_SQUARE, 440.0f, 0.25f);
int triangle_ch = synthesizer_add_channel(synth, WAVE_TRIANGLE, 440.0f, 0.25f);
```

## Musical Note Frequencies

Common musical note frequencies for reference:

| Note | Frequency (Hz) |
|------|----------------|
| C4   | 261.63        |
| D4   | 293.66        |
| E4   | 329.63        |
| F4   | 349.23        |
| G4   | 392.00        |
| A4   | 440.00        |
| B4   | 493.88        |
| C5   | 523.25        |

## Building and Testing

### Compile the test program:
```bash
make synthesizer-test
```

### Compile the demo program:
```bash
make synthesizer-demo
```

### Run tests:
```bash
./synthesizer-test
./synthesizer-demo
```

## Technical Details

### Sample Rate
- Default: 44.1kHz (CD quality)
- Configurable on creation
- Affects timing precision for envelopes

### Audio Output
- 32-bit float samples
- Range: -1.0 to +1.0
- Master volume control: 0.0 to 1.0

### Performance
- Real-time capable
- Low CPU usage
- No dynamic memory allocation during audio generation

### Thread Safety
- Not thread-safe by default
- Single-threaded design for simplicity
- External synchronization required for multi-threaded use

## Integration with VERSE Engine

The synthesizer module is designed to integrate seamlessly with the VERSE game engine:

- **Audio System**: Provides real-time audio generation for game events
- **Sound Effects**: Generate procedural sound effects
- **Music System**: Create dynamic background music
- **Interactive Audio**: Respond to player actions and game state

## Future Enhancements

- **Filters**: Low-pass, high-pass, band-pass filters
- **Effects**: Reverb, delay, chorus, distortion
- **LFO**: Low-frequency oscillators for modulation
- **MIDI Support**: MIDI input/output for external control
- **Polyphony**: Automatic voice allocation
- **Effects Chain**: Modular effects processing
- **Sample Rate Conversion**: Support for different sample rates
- **Audio File Output**: Export to WAV, FLAC, etc.
