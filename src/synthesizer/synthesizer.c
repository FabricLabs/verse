#include "synthesizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TWO_PI 6.283185307179586f
#define MAX_POLYPHONY 8

Synthesizer* synthesizer_create(int sample_rate) {
    Synthesizer* synth = malloc(sizeof(Synthesizer));
    if (!synth) return NULL;

    synth->sample_rate = sample_rate;
    synth->master_volume = 1.0f;
    synth->active_channels = 0;

    // Initialize advanced timing
    synth->tempo = 120.0f; // Default 120 BPM
    synth->time_signature_numerator = 4.0f;
    synth->time_signature_denominator = 4.0f;
    synth->beat_timer = 0.0f;
    synth->measure_timer = 0.0f;
    synth->current_beat = 0;
    synth->current_measure = 0;

    // Initialize audio quality settings
    synth->master_filter_cutoff = 20000.0f;
    synth->master_filter_resonance = 0.0f;
    synth->compressor_threshold = 0.8f;
    synth->compressor_ratio = 4.0f;
    synth->compressor_attack = 0.01f;
    synth->compressor_release = 0.1f;
    synth->limiter_threshold = 0.95f;
    synth->peak_detector = 0.0f;
    synth->rms_detector = 0.0f;
    synth->normalization_factor = 1.0f;
    synth->auto_normalize = true;

    // Initialize audio buffers
    synth->buffer_size = 4096;
    synth->buffer_position = 0;
    synth->output_buffer = malloc(sizeof(float) * synth->buffer_size);
    if (!synth->output_buffer) {
        free(synth);
        return NULL;
    }

    // Initialize delay
    synth->delay_buffer_size = sample_rate * 2; // 2 seconds max delay
    synth->delay_position = 0;
    synth->delay_time = 0.0f;
    synth->delay_feedback = 0.0f;
    synth->delay_mix = 0.0f;
    synth->delay_buffer = malloc(sizeof(float) * synth->delay_buffer_size);
    if (!synth->delay_buffer) {
        free(synth->output_buffer);
        free(synth);
        return NULL;
    }
    memset(synth->delay_buffer, 0, sizeof(float) * synth->delay_buffer_size);

    // Initialize reverb
    synth->reverb_buffer_size = sample_rate * 3; // 3 seconds max reverb
    synth->reverb_position = 0;
    synth->reverb_time = 0.0f;
    synth->reverb_mix = 0.0f;
    synth->reverb_buffer = malloc(sizeof(float) * synth->reverb_buffer_size);
    if (!synth->reverb_buffer) {
        free(synth->delay_buffer);
        free(synth->output_buffer);
        free(synth);
        return NULL;
    }
    memset(synth->reverb_buffer, 0, sizeof(float) * synth->reverb_buffer_size);

    // Initialize all channels
    for (int i = 0; i < 16; i++) {
        Channel* channel = &synth->channels[i];
        channel->wave_type = WAVE_SINE;
        channel->frequency = 440.0f;
        channel->amplitude = 0.3f; // Reduced amplitude to prevent clipping
        channel->phase = 0.0f;
        channel->active = false;
        channel->note_on = false;
        channel->program = 0; // Default to Acoustic Grand Piano

        // ADSR defaults - improved envelope
        channel->attack_time = 0.02f;  // Faster attack for better response
        channel->decay_time = 0.1f;    // Shorter decay
        channel->sustain_level = 0.7f; // Higher sustain for better sustain
        channel->release_time = 0.2f;  // Shorter release
        channel->envelope_time = 0.0f;
        channel->envelope_level = 0.0f;

        // Advanced features
        channel->articulation = ARTICULATION_NORMAL;
        channel->syncopation_offset = 0.0f;
        channel->advanced_sustain_level = 0.8f;
        channel->sustain_time = 0.5f;
        channel->sustain_timer = 0.0f;
        channel->is_sustained = false;
        channel->accent_level = 0;
        channel->velocity = 0.8f;
        channel->duration = 0.25f;
        channel->syncopated = false;

        // Sound quality improvements
        channel->filter_type = FILTER_NONE;
        channel->filter_cutoff = 20000.0f;
        channel->filter_resonance = 0.0f;
        channel->filter_env_amount = 0.0f;
        channel->filter_env_decay = 0.1f;
        channel->last_sample = 0.0f;
        channel->dc_offset = 0.0f;
        channel->anti_alias_accumulator = 0.0f;

        // Portamento
        channel->target_frequency = 440.0f;
        channel->portamento_time = 0.0f;
        channel->portamento_rate = 0.0f;
        channel->portamento_active = false;

        // LFO
        channel->lfo_frequency = 0.0f;
        channel->lfo_depth = 0.0f;
        channel->lfo_phase = 0.0f;
        channel->lfo_enabled = false;

        // Polyphony support
        channel->voice_count = 1;
        channel->voice_frequencies = malloc(sizeof(float) * MAX_POLYPHONY);
        channel->voice_phases = malloc(sizeof(float) * MAX_POLYPHONY);
        channel->voice_envelopes = malloc(sizeof(float) * MAX_POLYPHONY);
        channel->voice_active = malloc(sizeof(bool) * MAX_POLYPHONY);

        if (!channel->voice_frequencies || !channel->voice_phases ||
            !channel->voice_envelopes || !channel->voice_active) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(synth->channels[j].voice_frequencies);
                free(synth->channels[j].voice_phases);
                free(synth->channels[j].voice_envelopes);
                free(synth->channels[j].voice_active);
            }
            free(synth->reverb_buffer);
            free(synth->delay_buffer);
            free(synth->output_buffer);
            free(synth);
            return NULL;
        }

        // Initialize polyphony arrays
        for (int j = 0; j < MAX_POLYPHONY; j++) {
            channel->voice_frequencies[j] = 440.0f;
            channel->voice_phases[j] = 0.0f;
            channel->voice_envelopes[j] = 0.0f;
            channel->voice_active[j] = false;
        }
    }

    return synth;
}

void synthesizer_destroy(Synthesizer* synth) {
    if (!synth) return;

    // Free polyphony arrays
    for (int i = 0; i < 16; i++) {
        free(synth->channels[i].voice_frequencies);
        free(synth->channels[i].voice_phases);
        free(synth->channels[i].voice_envelopes);
        free(synth->channels[i].voice_active);
    }

    // Free audio buffers
    if (synth->output_buffer) free(synth->output_buffer);
    if (synth->delay_buffer) free(synth->delay_buffer);
    if (synth->reverb_buffer) free(synth->reverb_buffer);

    free(synth);
}

int synthesizer_add_channel(Synthesizer* synth, WaveType wave_type, float frequency, float amplitude) {
    if (!synth) return -1;

    for (int i = 0; i < 16; i++) {
        if (!synth->channels[i].active) {
            Channel* channel = &synth->channels[i];
            channel->wave_type = wave_type;
            channel->frequency = frequency;
            channel->amplitude = amplitude;
            channel->active = true;
            synth->active_channels++;
            return i;
        }
    }
    return -1;
}

void synthesizer_remove_channel(Synthesizer* synth, int channel_id) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];
    if (channel->active) {
        channel->active = false;
        synth->active_channels--;
    }
}

// Sound quality improvement functions
void synthesizer_set_channel_filter(Synthesizer* synth, int channel_id, FilterType type, float cutoff, float resonance) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];
    channel->filter_type = type;
    channel->filter_cutoff = fmaxf(20.0f, fminf(20000.0f, cutoff));
    channel->filter_resonance = fmaxf(0.0f, fminf(1.0f, resonance));
}

void synthesizer_set_channel_portamento(Synthesizer* synth, int channel_id, float time) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];
    channel->portamento_time = fmaxf(0.0f, time);
    if (time > 0.0f) {
        channel->portamento_rate = fabsf(channel->target_frequency - channel->frequency) / time;
    } else {
        channel->portamento_rate = 0.0f;
    }
}

void synthesizer_set_channel_lfo(Synthesizer* synth, int channel_id, float frequency, float depth) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];
    channel->lfo_frequency = fmaxf(0.0f, fminf(20.0f, frequency));
    channel->lfo_depth = fmaxf(0.0f, fminf(1.0f, depth));
    channel->lfo_enabled = (frequency > 0.0f && depth > 0.0f);
}

void synthesizer_set_master_filter(Synthesizer* synth, float cutoff, float resonance) {
    if (!synth) return;
    synth->master_filter_cutoff = fmaxf(20.0f, fminf(20000.0f, cutoff));
    synth->master_filter_resonance = fmaxf(0.0f, fminf(1.0f, resonance));
}

void synthesizer_set_compressor(Synthesizer* synth, float threshold, float ratio, float attack, float release) {
    if (!synth) return;
    synth->compressor_threshold = fmaxf(0.0f, fminf(1.0f, threshold));
    synth->compressor_ratio = fmaxf(1.0f, ratio);
    synth->compressor_attack = fmaxf(0.001f, attack);
    synth->compressor_release = fmaxf(0.001f, release);
}

void synthesizer_set_limiter(Synthesizer* synth, float threshold) {
    if (!synth) return;
    synth->limiter_threshold = fmaxf(0.0f, fminf(1.0f, threshold));
}

void synthesizer_set_delay(Synthesizer* synth, float time, float feedback, float mix) {
    if (!synth) return;
    synth->delay_time = fmaxf(0.0f, fminf(2.0f, time));
    synth->delay_feedback = fmaxf(0.0f, fminf(0.9f, feedback));
    synth->delay_mix = fmaxf(0.0f, fminf(1.0f, mix));
}

void synthesizer_set_reverb(Synthesizer* synth, float time, float mix) {
    if (!synth) return;
    synth->reverb_time = fmaxf(0.0f, fminf(3.0f, time));
    synth->reverb_mix = fmaxf(0.0f, fminf(1.0f, mix));
}

void synthesizer_set_auto_normalize(Synthesizer* synth, bool enabled) {
    if (!synth) return;
    synth->auto_normalize = enabled;
}

// Enhanced frequency setting with portamento
void synthesizer_set_channel_frequency(Synthesizer* synth, int channel_id, float frequency) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];

    // Set target frequency for portamento
    channel->target_frequency = fmaxf(20.0f, fminf(20000.0f, frequency));

    // If portamento is disabled, set frequency immediately
    if (channel->portamento_time <= 0.0f) {
        channel->frequency = channel->target_frequency;
    } else {
        // Start portamento
        channel->portamento_active = true;
        channel->portamento_rate = fabsf(channel->target_frequency - channel->frequency) / channel->portamento_time;
    }
}

void synthesizer_set_channel_amplitude(Synthesizer* synth, int channel_id, float amplitude) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;
    synth->channels[channel_id].amplitude = amplitude;
}

void synthesizer_set_channel_wave_type(Synthesizer* synth, int channel_id, WaveType wave_type) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;
    synth->channels[channel_id].wave_type = wave_type;
}

void synthesizer_set_channel_program(Synthesizer* synth, int channel_id, uint8_t program) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;
    synth->channels[channel_id].program = program;

    // Simplify: use sine wave for all instruments for now
    Channel* channel = &synth->channels[channel_id];
    channel->wave_type = WAVE_SINE;

    // Adjust amplitude based on instrument type
    if (program >= 0 && program <= 7) {
        // Piano family - softer
        channel->amplitude = 0.2f;
    } else if (program >= 32 && program <= 39) {
        // Bass family - deeper
        channel->amplitude = 0.25f;
    } else if (program >= 40 && program <= 47) {
        // Strings family - smooth
        channel->amplitude = 0.15f;
    } else if (program >= 48 && program <= 55) {
        // Ensemble family - smooth
        channel->amplitude = 0.15f;
    } else {
        // Default
        channel->amplitude = 0.2f;
    }
}

// Advanced channel features
void synthesizer_set_channel_articulation(Synthesizer* synth, int channel_id, ArticulationType articulation) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;
    synth->channels[channel_id].articulation = articulation;
}

void synthesizer_set_channel_syncopation(Synthesizer* synth, int channel_id, float offset) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;
    synth->channels[channel_id].syncopation_offset = offset;
    synth->channels[channel_id].syncopated = (offset > 0.0f);
}

void synthesizer_set_channel_sustain(Synthesizer* synth, int channel_id, float level, float time) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;
    Channel* channel = &synth->channels[channel_id];
    channel->advanced_sustain_level = level;
    channel->sustain_time = time;
    channel->is_sustained = true;
}

void synthesizer_set_channel_accent(Synthesizer* synth, int channel_id, int accent_level) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;
    synth->channels[channel_id].accent_level = accent_level;
}

void synthesizer_set_channel_velocity(Synthesizer* synth, int channel_id, float velocity) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;
    synth->channels[channel_id].velocity = velocity;
}

void synthesizer_set_channel_duration(Synthesizer* synth, int channel_id, float duration) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;
    synth->channels[channel_id].duration = duration;
}

void synthesizer_set_channel_adsr(Synthesizer* synth, int channel_id,
                                  float attack, float decay, float sustain, float release) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];
    channel->attack_time = attack;
    channel->decay_time = decay;
    channel->sustain_level = sustain;
    channel->release_time = release;
}

void synthesizer_note_on(Synthesizer* synth, int channel_id) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];
    channel->active = true;  // Mark channel as active
    channel->note_on = true;
    channel->envelope_time = 0.0f;
    channel->envelope_level = 0.0f;
    channel->sustain_timer = 0.0f;
}

void synthesizer_note_off(Synthesizer* synth, int channel_id) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];
    channel->note_on = false;
    channel->envelope_time = 0.0f;
    // Don't deactivate immediately - let the envelope handle the release
}

// Enhanced note functions with portamento support
void synthesizer_note_on_advanced(Synthesizer* synth, int channel_id, float velocity, float duration) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];

    // Set target frequency for portamento
    channel->target_frequency = channel->frequency;

    // Start portamento if enabled
    if (channel->portamento_time > 0.0f) {
        channel->portamento_active = true;
        channel->portamento_rate = fabsf(channel->target_frequency - channel->frequency) / channel->portamento_time;
    }

    // Set velocity and duration
    channel->velocity = fmaxf(0.0f, fminf(1.0f, velocity));
    channel->duration = fmaxf(0.0f, duration);

    // Reset envelope
    channel->envelope_time = 0.0f;
    channel->envelope_level = 0.0f;
    channel->note_on = true;
    channel->active = true;

    // Reset LFO phase
    if (channel->lfo_enabled) {
        channel->lfo_phase = 0.0f;
    }
}

void synthesizer_note_off_advanced(Synthesizer* synth, int channel_id) {
    if (!synth || channel_id < 0 || channel_id >= 16) return;

    Channel* channel = &synth->channels[channel_id];
    channel->note_on = false;
    channel->envelope_time = 0.0f; // Reset for release phase
}

// Advanced timing functions
void synthesizer_set_tempo(Synthesizer* synth, float bpm) {
    if (!synth) return;
    synth->tempo = bpm;
}

void synthesizer_set_time_signature(Synthesizer* synth, float numerator, float denominator) {
    if (!synth) return;
    synth->time_signature_numerator = numerator;
    synth->time_signature_denominator = denominator;
}

void synthesizer_update_timing(Synthesizer* synth, float delta_time) {
    if (!synth) return;

    float beat_duration = 60.0f / synth->tempo; // Seconds per beat
    synth->beat_timer += delta_time;

    if (synth->beat_timer >= beat_duration) {
        synth->beat_timer -= beat_duration;
        synth->current_beat++;

        if (synth->current_beat >= (int)synth->time_signature_numerator) {
            synth->current_beat = 0;
            synth->current_measure++;
        }
    }
}

// Enhanced wave generation functions with anti-aliasing
float generate_sine_wave(float frequency, float phase, float amplitude) {
    return amplitude * sinf(TWO_PI * phase);
}

// Anti-aliased saw wave using band-limited synthesis
float generate_saw_wave(float frequency, float phase, float amplitude) {
    // Use band-limited saw wave to reduce aliasing
    float t = fmodf(phase, 1.0f);
    float saw = 2.0f * t - 1.0f;

    // Apply low-pass filter to reduce high-frequency artifacts
    float cutoff = frequency * 0.8f; // Limit harmonics
    if (frequency > 8000.0f) {
        saw *= 0.5f; // Reduce amplitude for high frequencies
    }

    return amplitude * saw;
}

// Anti-aliased square wave
float generate_square_wave(float frequency, float phase, float amplitude) {
    float t = fmodf(phase, 1.0f);
    float square = (t < 0.5f) ? 1.0f : -1.0f;

    // Apply low-pass filter for high frequencies
    if (frequency > 6000.0f) {
        square *= 0.7f;
    }

    return amplitude * square;
}

// Anti-aliased triangle wave
float generate_triangle_wave(float frequency, float phase, float amplitude) {
    float t = fmodf(phase, 1.0f);
    float triangle;

    if (t < 0.5f) {
        triangle = 4.0f * t - 1.0f;
    } else {
        triangle = 3.0f - 4.0f * t;
    }

    // Apply low-pass filter for high frequencies
    if (frequency > 8000.0f) {
        triangle *= 0.6f;
    }

    return amplitude * triangle;
}

// New wave types
float generate_pulse_wave(float frequency, float phase, float amplitude, float duty_cycle) {
    float t = fmodf(phase, 1.0f);
    float pulse = (t < duty_cycle) ? 1.0f : -1.0f;

    // Apply low-pass filter for high frequencies
    if (frequency > 5000.0f) {
        pulse *= 0.8f;
    }

    return amplitude * pulse;
}

float generate_noise_wave(float amplitude) {
    // Simple white noise generator
    static unsigned int seed = 12345;
    seed = seed * 1103515245 + 12345;
    float noise = (float)((int)(seed / 65536) % 32768) / 16384.0f - 1.0f;

    return amplitude * noise;
}

// Audio processing functions
float apply_filter(float input, float cutoff, float resonance, FilterType type) {
    static float filter_buffer[2] = {0.0f, 0.0f};
    static float filter_coeffs[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Simple IIR filter implementation
    float sample_rate = 44100.0f;
    float freq = cutoff / sample_rate;
    float q = resonance;

    if (type == FILTER_LOWPASS) {
        float w0 = TWO_PI * freq;
        float alpha = sinf(w0) / (2.0f * q);
        float b0 = (1.0f - cosf(w0)) / 2.0f;
        float b1 = 1.0f - cosf(w0);
        float b2 = (1.0f - cosf(w0)) / 2.0f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cosf(w0);
        float a2 = 1.0f - alpha;

        filter_coeffs[0] = b0 / a0;
        filter_coeffs[1] = b1 / a0;
        filter_coeffs[2] = b2 / a0;
        filter_coeffs[3] = a1 / a0;
    }

    // Apply filter
    float output = filter_coeffs[0] * input +
                   filter_coeffs[1] * filter_buffer[0] +
                   filter_coeffs[2] * filter_buffer[1] -
                   filter_coeffs[3] * filter_buffer[0];

    filter_buffer[1] = filter_buffer[0];
    filter_buffer[0] = input;

    return output;
}

float apply_compressor(float input, float threshold, float ratio, float attack, float release) {
    static float envelope = 0.0f;
    static float gain = 1.0f;

    float input_abs = fabsf(input);

    // Envelope follower
    if (input_abs > envelope) {
        envelope += attack * (input_abs - envelope);
    } else {
        envelope += release * (input_abs - envelope);
    }

    // Compression curve
    if (envelope > threshold) {
        float over_threshold = envelope - threshold;
        float compression = over_threshold * (1.0f - 1.0f / ratio);
        gain = (envelope - compression) / envelope;
    } else {
        gain = 1.0f;
    }

    return input * gain;
}

float apply_limiter(float input, float threshold) {
    float input_abs = fabsf(input);
    if (input_abs > threshold) {
        return (input > 0.0f ? threshold : -threshold);
    }
    return input;
}

float apply_delay(float input, float* buffer, int buffer_size, int* position, float feedback, float mix) {
    if (!buffer || buffer_size <= 0) return input;

    int delay_samples = (int)(0.1f * 44100.0f); // 100ms delay
    int read_pos = (*position - delay_samples + buffer_size) % buffer_size;

    float delayed = buffer[read_pos];
    buffer[*position] = input + delayed * feedback;

    *position = (*position + 1) % buffer_size;

    return input * (1.0f - mix) + delayed * mix;
}

float apply_reverb(float input, float* buffer, int buffer_size, int* position, float time, float mix) {
    if (!buffer || buffer_size <= 0) return input;

    // Simple reverb using multiple delay taps
    float reverb = 0.0f;
    int taps[] = {1000, 1500, 2000, 2500, 3000};
    float tap_gains[] = {0.3f, 0.2f, 0.15f, 0.1f, 0.05f};

    for (int i = 0; i < 5; i++) {
        int tap_pos = (*position - taps[i] + buffer_size) % buffer_size;
        reverb += buffer[tap_pos] * tap_gains[i];
    }

    buffer[*position] = input + reverb * time;
    *position = (*position + 1) % buffer_size;

    return input * (1.0f - mix) + reverb * mix;
}

// Enhanced ADSR envelope with sustain
float update_adsr_envelope(Channel* channel, float delta_time) {
    if (!channel->note_on) {
        // Release phase
        channel->envelope_time += delta_time;
        if (channel->envelope_time >= channel->release_time) {
            channel->envelope_level = 0.0f;
            channel->active = false;  // Deactivate channel after release
        } else {
            channel->envelope_level = channel->sustain_level *
                (1.0f - channel->envelope_time / channel->release_time);
        }
    } else {
        // Attack/Decay/Sustain phases
        if (channel->envelope_level < 1.0f && channel->envelope_time < channel->attack_time) {
            // Attack phase with improved curve
            channel->envelope_time += delta_time;
            float attack_progress = channel->envelope_time / channel->attack_time;
            channel->envelope_level = 1.0f - expf(-attack_progress * 3.0f); // Exponential attack
        } else if (channel->envelope_level > channel->sustain_level) {
            // Decay phase with improved curve
            channel->envelope_time += delta_time;
            float decay_progress = (channel->envelope_time - channel->attack_time) / channel->decay_time;
            channel->envelope_level = 1.0f - (1.0f - channel->sustain_level) * (1.0f - expf(-decay_progress * 2.0f));
            if (channel->envelope_level < channel->sustain_level) {
                channel->envelope_level = channel->sustain_level;
            }
        }

        // Handle sustain phase
        if (channel->is_sustained) {
            channel->sustain_timer += delta_time;
            if (channel->sustain_timer < channel->sustain_time) {
                // Apply sustain envelope
                float sustain_env = 1.0f - (channel->sustain_timer / channel->sustain_time) * (1.0f - channel->advanced_sustain_level);
                channel->envelope_level *= sustain_env;
            }
        }
    }

    return channel->envelope_level;
}

// Generate a single sample with enhanced sound quality
float synthesizer_generate_sample(Synthesizer* synth) {
    if (!synth) return 0.0f;

    float sample = 0.0f;
    float delta_time = 1.0f / synth->sample_rate;
    bool any_active_notes = false;

    for (int i = 0; i < 16; i++) {
        Channel* channel = &synth->channels[i];
        if (!channel->active) continue;

        // Update envelope
        float envelope = update_adsr_envelope(channel, delta_time);

        // Only generate audio if envelope level is above threshold
        if (envelope > 0.001f) {
            any_active_notes = true;

            // Apply articulation effects
            float articulation_multiplier = 1.0f;
            switch (channel->articulation) {
                case ARTICULATION_STACCATO:
                    articulation_multiplier = 0.6f; // Shorter, punchier notes
                    break;
                case ARTICULATION_LEGATO:
                    articulation_multiplier = 1.2f; // Longer, smoother notes
                    break;
                case ARTICULATION_ACCENT:
                    articulation_multiplier = 1.5f; // Louder, emphasized notes
                    break;
                case ARTICULATION_SYNCOPATED:
                    articulation_multiplier = 1.1f; // Slightly emphasized syncopated notes
                    break;
                case ARTICULATION_PORTAMENTO:
                    articulation_multiplier = 1.0f; // Normal for portamento
                    break;
                default:
                    articulation_multiplier = 1.0f;
                    break;
            }

            // Apply accent
            if (channel->accent_level > 0) {
                articulation_multiplier *= (1.0f + channel->accent_level * 0.3f);
            }

            // Handle portamento
            if (channel->portamento_active) {
                channel->frequency += channel->portamento_rate * delta_time;
                if (fabsf(channel->frequency - channel->target_frequency) < 1.0f) {
                    channel->frequency = channel->target_frequency;
                    channel->portamento_active = false;
                }
            }

            // Update LFO
            if (channel->lfo_enabled) {
                channel->lfo_phase += channel->lfo_frequency * delta_time;
                if (channel->lfo_phase >= 1.0f) {
                    channel->lfo_phase -= 1.0f;
                }
                float lfo_mod = sinf(TWO_PI * channel->lfo_phase) * channel->lfo_depth;
                channel->frequency *= (1.0f + lfo_mod);
            }

            // Generate wave based on type with anti-aliasing
            float wave_sample = 0.0f;
            switch (channel->wave_type) {
                case WAVE_SINE:
                    wave_sample = generate_sine_wave(channel->frequency, channel->phase, 1.0f);
                    break;
                case WAVE_SAW:
                    wave_sample = generate_saw_wave(channel->frequency, channel->phase, 1.0f);
                    break;
                case WAVE_SQUARE:
                    wave_sample = generate_square_wave(channel->frequency, channel->phase, 1.0f);
                    break;
                case WAVE_TRIANGLE:
                    wave_sample = generate_triangle_wave(channel->frequency, channel->phase, 1.0f);
                    break;
                case WAVE_PULSE:
                    wave_sample = generate_pulse_wave(channel->frequency, channel->phase, 1.0f, 0.5f);
                    break;
                case WAVE_NOISE:
                    wave_sample = generate_noise_wave(1.0f);
                    break;
            }

            // Apply anti-aliasing filter
            if (channel->filter_type != FILTER_NONE) {
                wave_sample = apply_filter(wave_sample, channel->filter_cutoff, channel->filter_resonance, channel->filter_type);
            }

            // Apply envelope, articulation, and velocity
            float final_amplitude = channel->amplitude * envelope * articulation_multiplier * channel->velocity;
            float channel_sample = wave_sample * final_amplitude;

            // DC offset removal (per-channel)
            channel_sample -= channel->dc_offset;
            channel->dc_offset = channel->dc_offset * 0.999f + channel_sample * 0.001f;

            sample += channel_sample;

            // Update phase with anti-aliasing
            float phase_increment = channel->frequency * delta_time;
            channel->phase += phase_increment;
            if (channel->phase >= 1.0f) {
                channel->phase -= 1.0f;
            }

            // Anti-aliasing accumulator
            channel->anti_alias_accumulator += phase_increment;
            if (channel->anti_alias_accumulator > 0.5f) {
                channel->anti_alias_accumulator -= 1.0f;
            }
        }
    }

    // Apply master effects
    if (any_active_notes) {
        // Apply master filter
        if (synth->master_filter_cutoff < 20000.0f) {
            sample = apply_filter(sample, synth->master_filter_cutoff, synth->master_filter_resonance, FILTER_LOWPASS);
        }

        // Apply compressor
        sample = apply_compressor(sample, synth->compressor_threshold, synth->compressor_ratio,
                                synth->compressor_attack, synth->compressor_release);

        // Apply limiter
        sample = apply_limiter(sample, synth->limiter_threshold);

        // Apply delay
        if (synth->delay_time > 0.0f) {
            sample = apply_delay(sample, synth->delay_buffer, synth->delay_buffer_size,
                               &synth->delay_position, synth->delay_feedback, synth->delay_mix);
        }

        // Apply reverb
        if (synth->reverb_time > 0.0f) {
            sample = apply_reverb(sample, synth->reverb_buffer, synth->reverb_buffer_size,
                                &synth->reverb_position, synth->reverb_time, synth->reverb_mix);
        }

        // Update peak and RMS detectors
        float sample_abs = fabsf(sample);
        if (sample_abs > synth->peak_detector) {
            synth->peak_detector = sample_abs;
        } else {
            synth->peak_detector *= 0.999f;
        }

        synth->rms_detector = synth->rms_detector * 0.999f + sample * sample * 0.001f;

        // Auto-normalize
        if (synth->auto_normalize && synth->peak_detector > 0.0f) {
            float target_peak = 0.8f;
            if (synth->peak_detector > target_peak) {
                synth->normalization_factor = target_peak / synth->peak_detector;
            } else {
                synth->normalization_factor = 1.0f;
            }
            sample *= synth->normalization_factor;
        }

        // Apply master volume
        sample *= synth->master_volume;
    }

    return sample;
}

void synthesizer_generate_buffer(Synthesizer* synth, float* buffer, int num_samples) {
    if (!synth || !buffer) return;

    for (int i = 0; i < num_samples; i++) {
        buffer[i] = synthesizer_generate_sample(synth);
    }
}

void synthesizer_set_master_volume(Synthesizer* synth, float volume) {
    if (!synth) return;
    synth->master_volume = fmaxf(0.0f, fminf(1.0f, volume));
}

int synthesizer_get_active_channels(Synthesizer* synth) {
    return synth ? synth->active_channels : 0;
}

bool synthesizer_is_channel_active(Synthesizer* synth, int channel_id) {
    if (!synth || channel_id < 0 || channel_id >= 16) return false;
    return synth->channels[channel_id].active;
}

float synthesizer_get_channel_amplitude(Synthesizer* synth, int channel_id) {
    if (!synth || channel_id < 0 || channel_id >= 16) return 0.0f;
    return synth->channels[channel_id].amplitude;
}
