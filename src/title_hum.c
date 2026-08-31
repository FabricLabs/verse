#include "title_hum.h"
#include "synthesizer/synthesizer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Musical reference: F#3 ≈ 185.00 Hz, F#2 ≈ 92.50 Hz
static const float DEFAULT_FSHARP3_HZ = 185.00f;
static const float DEFAULT_FSHARP2_HZ = 92.50f;
static const float GOLDEN_RATIO = 1.6180339887f;

// Simple utility: smoothstep (cubic Hermite) 0..1
static inline float smoothstep01(float t)
{
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// Exponential ease-out curve 0..1
static inline float ease_out_exp01(float t)
{
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  return 1.0f - powf(2.0f, -10.0f * t);
}

TitleHumSystem *title_hum_create(int sample_rate)
{
  TitleHumSystem *hum = (TitleHumSystem *)calloc(1, sizeof(TitleHumSystem));
  if (!hum)
    return NULL;

  hum->sample_rate = sample_rate;
  hum->synth = synthesizer_create(sample_rate);
  if (!hum->synth)
  {
    free(hum);
    return NULL;
  }

  // Set subtle master processing to avoid harshness
  synthesizer_set_master_filter(hum->synth, 16000.0f, 0.1f);
  synthesizer_set_reverb(hum->synth, 0.2f, 0.08f);
  synthesizer_set_limiter(hum->synth, 0.9f);
  synthesizer_set_master_volume(hum->synth, 1.0f);

  // Start/end frequencies: slope down one octave F#3 -> F#2
  hum->start_frequency_hz = DEFAULT_FSHARP3_HZ;
  hum->end_frequency_hz = DEFAULT_FSHARP2_HZ;

  // Target is 5s; harmonize at the smaller remainder wrt golden ratio
  hum->duration_sec = 5.0f;
  float golden_smaller = hum->duration_sec / GOLDEN_RATIO; // ~3.09s
  hum->harmonize_time_sec = golden_smaller;
  hum->playback_time_sec = 0.0f;
  hum->active = false;
  hum->started = false;

  // Allocate 7 channels as pure sine harmonics (partials 1..7)
  // Initial amplitude set to 0. Actual amplitude shaped over time in generate.
  const struct
  {
    WaveType wave;
    float amp;
  } voices[7] = {
      {WAVE_SINE, 0.0f},
      {WAVE_SINE, 0.0f},
      {WAVE_SINE, 0.0f},
      {WAVE_SINE, 0.0f},
      {WAVE_SINE, 0.0f},
      {WAVE_SINE, 0.0f},
      {WAVE_SINE, 0.0f}};

  for (int i = 0; i < 7; i++)
  {
    hum->channel_ids[i] = synthesizer_add_channel(hum->synth, voices[i].wave, hum->start_frequency_hz, voices[i].amp);
    if (hum->channel_ids[i] >= 0)
    {
      // Fast ADSR to let our manual amplitude shape control the swell
      synthesizer_set_channel_adsr(hum->synth, hum->channel_ids[i], 0.01f, 0.05f, 1.0f, 0.5f);
      // No per-channel filter/LFO for a clean, warm sine stack
      synthesizer_set_channel_filter(hum->synth, hum->channel_ids[i], FILTER_NONE, 20000.0f, 0.0f);
    }
  }

  return hum;
}

void title_hum_destroy(TitleHumSystem *hum)
{
  if (!hum)
    return;
  if (hum->synth)
  {
    synthesizer_destroy(hum->synth);
  }
  free(hum);
}

void title_hum_start(TitleHumSystem *hum)
{
  if (!hum || hum->active)
    return;
  hum->active = true;
  hum->started = true;
  hum->playback_time_sec = 0.0f;

  // Begin all 7 voices
  for (int i = 0; i < 7; i++)
  {
    if (hum->channel_ids[i] >= 0)
    {
      // Soft initial amplitude; ADSR will fade in
      synthesizer_set_channel_amplitude(hum->synth, hum->channel_ids[i], synthesizer_get_channel_amplitude(hum->synth, hum->channel_ids[i]));
      synthesizer_note_on(hum->synth, hum->channel_ids[i]);
    }
  }
}

void title_hum_stop(TitleHumSystem *hum)
{
  if (!hum)
    return;
  hum->active = false;
  // Release voices
  for (int i = 0; i < 7; i++)
  {
    if (hum->channel_ids[i] >= 0)
    {
      synthesizer_note_off(hum->synth, hum->channel_ids[i]);
    }
  }
}

bool title_hum_is_active(TitleHumSystem *hum)
{
  return hum && hum->active;
}

// Helper to compute sloped frequency with a curved glide that settles by harmonize_time_sec
static inline float compute_glide_freq(const TitleHumSystem *hum, float t_sec)
{
  float t = fminf(fmaxf(t_sec / hum->harmonize_time_sec, 0.0f), 1.0f);
  // Use exponential ease-out then smoothstep for a natural glide
  float curve = smoothstep01(ease_out_exp01(t));
  float f = hum->start_frequency_hz + (hum->end_frequency_hz - hum->start_frequency_hz) * curve;
  return f;
}

void title_hum_generate_buffer(TitleHumSystem *hum, float *buffer, int num_samples)
{
  if (!buffer || num_samples <= 0)
    return;

  // Default to silence
  if (!hum || !hum->synth)
  {
    memset(buffer, 0, sizeof(float) * (size_t)num_samples);
    return;
  }

  // Update frequency glide and harmonic layering per-sample window
  if (hum->active)
  {
    // Determine per-sample time step
    float dt = 1.0f / (float)hum->sample_rate;

    // Before harmonize point: glide; after: gently hold final warm note then fade
    for (int i = 0; i < num_samples; i++)
    {
      float current_time = hum->playback_time_sec + (float)i * dt;

      float base_freq = (current_time < hum->harmonize_time_sec)
                            ? compute_glide_freq(hum, current_time)
                            : hum->end_frequency_hz;

      // Global fade centered at golden point
      float fade_out = 1.0f;
      if (current_time > hum->harmonize_time_sec)
      {
        float tail = hum->duration_sec - hum->harmonize_time_sec;
        float u = fminf(fmaxf((current_time - hum->harmonize_time_sec) / fmaxf(tail, 0.0001f), 0.0f), 1.0f);
        fade_out = 1.0f - smoothstep01(u);
      }

      // Harmonic weights (partials 1..7) – warmer emphasis on low partials
      const float base_weights[7] = {0.22f, 0.18f, 0.14f, 0.11f, 0.08f, 0.06f, 0.05f};

      // Layering: later onsets for higher partials so they "arrive" approaching golden time
      const float ramp_in = 0.7f; // seconds to reach full per-partial level
      for (int h = 0; h < 7; h++)
      {
        int ch = hum->channel_ids[h];
        if (ch < 0)
          continue;

        // Partial frequency = (h+1) * base
        float f = base_freq * (float)(h + 1);
        synthesizer_set_channel_frequency(hum->synth, ch, f);

        // Onset schedule: fundamental starts earlier, higher partials later
        float latest_onset = fmaxf(hum->harmonize_time_sec - ramp_in, 0.0f);
        float onset = latest_onset * ((float)h / 6.0f); // h=0 -> 0, h=6 -> ~latest_onset
        float up = smoothstep01((current_time - onset) / ramp_in);

        float amp = base_weights[h] * up * fade_out;
        // Conservative global gain
        amp *= 0.8f;
        synthesizer_set_channel_amplitude(hum->synth, ch, amp);
      }
    }

    hum->playback_time_sec += (float)num_samples / (float)hum->sample_rate;

    // Auto-stop when duration reached, but leave final warm note sustaining briefly via ADSR release
    if (hum->playback_time_sec >= hum->duration_sec)
    {
      title_hum_stop(hum);
    }
  }

  // Generate audio from internal synth into buffer
  synthesizer_generate_buffer(hum->synth, buffer, num_samples);
}
