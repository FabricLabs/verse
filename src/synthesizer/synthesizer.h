#ifndef SYNTHESIZER_H
#define SYNTHESIZER_H

#include <stdint.h>
#include <stdbool.h>

// Wave types
typedef enum {
    WAVE_SINE,
    WAVE_SAW,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_PULSE,
    WAVE_NOISE
} WaveType;

// Advanced articulation types
typedef enum {
    ARTICULATION_NORMAL,
    ARTICULATION_STACCATO,
    ARTICULATION_LEGATO,
    ARTICULATION_ACCENT,
    ARTICULATION_SYNCOPATED,
    ARTICULATION_PORTAMENTO
} ArticulationType;

// Filter types for sound quality
typedef enum {
    FILTER_NONE,
    FILTER_LOWPASS,
    FILTER_HIGHPASS,
    FILTER_BANDPASS,
    FILTER_NOTCH
} FilterType;

// Enhanced channel structure with improved sound quality
typedef struct {
    WaveType wave_type;
    float frequency;
    float amplitude;
    float phase;
    bool active;
    uint8_t program;  // MIDI program number (instrument)

    // ADSR envelope with improved shaping
    float attack_time;
    float decay_time;
    float sustain_level;
    float release_time;
    float envelope_time;
    float envelope_level;
    bool note_on;

    // Advanced features
    ArticulationType articulation;
    float syncopation_offset;
    float advanced_sustain_level;
    float sustain_time;
    float sustain_timer;
    bool is_sustained;
    int accent_level;
    float velocity;
    float duration;
    bool syncopated;

    // Sound quality improvements
    FilterType filter_type;
    float filter_cutoff;
    float filter_resonance;
    float filter_env_amount;
    float filter_env_decay;

    // Anti-aliasing and audio quality
    float last_sample;
    float dc_offset;
    float anti_alias_accumulator;

    // Portamento
    float target_frequency;
    float portamento_time;
    float portamento_rate;
    bool portamento_active;

    // Advanced modulation
    float lfo_frequency;
    float lfo_depth;
    float lfo_phase;
    bool lfo_enabled;

    // Polyphony support
    int voice_count;
    float* voice_frequencies;
    float* voice_phases;
    float* voice_envelopes;
    bool* voice_active;
} Channel;

// Synthesizer structure with improved audio processing.
// Tagged so that `struct Synthesizer` forward declarations (title_hum.h) name this
// same type; with an untagged typedef the two are unrelated types to the compiler.
typedef struct Synthesizer {
    Channel channels[16];  // Support up to 16 channels
    int sample_rate;
    float master_volume;
    int active_channels;

    // Advanced timing
    float tempo;           // BPM
    float time_signature_numerator;
    float time_signature_denominator;
    float beat_timer;
    float measure_timer;
    int current_beat;
    int current_measure;

    // Audio quality improvements
    float master_filter_cutoff;
    float master_filter_resonance;
    float compressor_threshold;
    float compressor_ratio;
    float compressor_attack;
    float compressor_release;
    float limiter_threshold;

    // Anti-clipping and normalization
    float peak_detector;
    float rms_detector;
    float normalization_factor;
    bool auto_normalize;

    // Audio buffer management
    float* output_buffer;
    int buffer_size;
    int buffer_position;

    // Advanced audio processing
    float* delay_buffer;
    int delay_buffer_size;
    int delay_position;
    float delay_time;
    float delay_feedback;
    float delay_mix;

    // Reverb
    float* reverb_buffer;
    int reverb_buffer_size;
    int reverb_position;
    float reverb_time;
    float reverb_mix;
} Synthesizer;

// Function declarations
Synthesizer* synthesizer_create(int sample_rate);
void synthesizer_destroy(Synthesizer* synth);

// Channel management
int synthesizer_add_channel(Synthesizer* synth, WaveType wave_type, float frequency, float amplitude);
void synthesizer_remove_channel(Synthesizer* synth, int channel_id);
void synthesizer_set_channel_frequency(Synthesizer* synth, int channel_id, float frequency);
void synthesizer_set_channel_amplitude(Synthesizer* synth, int channel_id, float amplitude);
void synthesizer_set_channel_wave_type(Synthesizer* synth, int channel_id, WaveType wave_type);
void synthesizer_set_channel_program(Synthesizer* synth, int channel_id, uint8_t program);

// Advanced channel features
void synthesizer_set_channel_articulation(Synthesizer* synth, int channel_id, ArticulationType articulation);
void synthesizer_set_channel_syncopation(Synthesizer* synth, int channel_id, float offset);
void synthesizer_set_channel_sustain(Synthesizer* synth, int channel_id, float level, float time);
void synthesizer_set_channel_accent(Synthesizer* synth, int channel_id, int accent_level);
void synthesizer_set_channel_velocity(Synthesizer* synth, int channel_id, float velocity);
void synthesizer_set_channel_duration(Synthesizer* synth, int channel_id, float duration);

// Sound quality improvements
void synthesizer_set_channel_filter(Synthesizer* synth, int channel_id, FilterType type, float cutoff, float resonance);
void synthesizer_set_channel_portamento(Synthesizer* synth, int channel_id, float time);
void synthesizer_set_channel_lfo(Synthesizer* synth, int channel_id, float frequency, float depth);
void synthesizer_set_master_filter(Synthesizer* synth, float cutoff, float resonance);
void synthesizer_set_compressor(Synthesizer* synth, float threshold, float ratio, float attack, float release);
void synthesizer_set_limiter(Synthesizer* synth, float threshold);
void synthesizer_set_delay(Synthesizer* synth, float time, float feedback, float mix);
void synthesizer_set_reverb(Synthesizer* synth, float time, float mix);
void synthesizer_set_auto_normalize(Synthesizer* synth, bool enabled);

// ADSR envelope functions
void synthesizer_set_channel_adsr(Synthesizer* synth, int channel_id,
                                  float attack, float decay, float sustain, float release);
void synthesizer_note_on(Synthesizer* synth, int channel_id);
void synthesizer_note_off(Synthesizer* synth, int channel_id);

// Advanced note functions
void synthesizer_note_on_advanced(Synthesizer* synth, int channel_id, float velocity, float duration);
void synthesizer_note_off_advanced(Synthesizer* synth, int channel_id);

// Audio generation
float synthesizer_generate_sample(Synthesizer* synth);
void synthesizer_generate_buffer(Synthesizer* synth, float* buffer, int num_samples);

// Wave generation functions with anti-aliasing
float generate_sine_wave(float frequency, float phase, float amplitude);
float generate_saw_wave(float frequency, float phase, float amplitude);
float generate_square_wave(float frequency, float phase, float amplitude);
float generate_triangle_wave(float frequency, float phase, float amplitude);
float generate_pulse_wave(float frequency, float phase, float amplitude, float duty_cycle);
float generate_noise_wave(float amplitude);

// Audio processing functions
float apply_filter(float input, float cutoff, float resonance, FilterType type);
float apply_compressor(float input, float threshold, float ratio, float attack, float release);
float apply_limiter(float input, float threshold);
float apply_delay(float input, float* buffer, int buffer_size, int* position, float feedback, float mix);
float apply_reverb(float input, float* buffer, int buffer_size, int* position, float time, float mix);

// Advanced timing functions
void synthesizer_set_tempo(Synthesizer* synth, float bpm);
void synthesizer_set_time_signature(Synthesizer* synth, float numerator, float denominator);
void synthesizer_update_timing(Synthesizer* synth, float delta_time);

// Utility functions
void synthesizer_set_master_volume(Synthesizer* synth, float volume);
int synthesizer_get_active_channels(Synthesizer* synth);
bool synthesizer_is_channel_active(Synthesizer* synth, int channel_id);
float synthesizer_get_channel_amplitude(Synthesizer* synth, int channel_id);

#endif // SYNTHESIZER_H
