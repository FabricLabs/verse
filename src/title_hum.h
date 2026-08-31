#ifndef TITLE_HUM_H
#define TITLE_HUM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward-declare Synthesizer without redefining typedef
struct Synthesizer;

typedef struct {
  int sample_rate;
  struct Synthesizer* synth;
  int channel_ids[7];
  float start_frequency_hz;   // Starting frequency (F#3 ~ 185.00 Hz)
  float end_frequency_hz;     // Ending frequency (F#2 ~ 92.50 Hz)
  float playback_time_sec;
  float duration_sec;         // Total target duration, default 5.0s
  float harmonize_time_sec;   // Time at which harmonization occurs (golden point)
  bool active;
  bool started;
} TitleHumSystem;

// Lifecycle
TitleHumSystem* title_hum_create(int sample_rate);
void title_hum_destroy(TitleHumSystem* hum);

// Control
void title_hum_start(TitleHumSystem* hum);
void title_hum_stop(TitleHumSystem* hum);
bool title_hum_is_active(TitleHumSystem* hum);

// Audio generation
// Writes num_samples float samples into buffer (mono). Always safe to call; writes silence if inactive/null
void title_hum_generate_buffer(TitleHumSystem* hum, float* buffer, int num_samples);

#ifdef __cplusplus
}
#endif

#endif // TITLE_HUM_H


