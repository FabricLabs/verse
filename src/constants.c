#include "constants.h"
#include <string.h>

// Define known conditions here; index is bit position
static const char* _KNOWN_CONDITIONS[] = {
  "WET",
  "FERTILIZED",
  "SEEDED",
  "TALL",
  "FLOWERING",
  "DECAYING",
  "DECAYED",
  "RADIOACTIVE",
  "BURNING_LOW",
  "BURNING_MEDIUM",
  "BURNING_HIGH",
  NULL
};

const unsigned int KNOWN_CONDITIONS_COUNT = 11;
const char* KNOWN_CONDITIONS[] = {
  "WET",
  "FERTILIZED",
  "SEEDED",
  "TALL",
  "FLOWERING",
  "DECAYING",
  "DECAYED",
  "RADIOACTIVE",
  "BURNING_LOW",
  "BURNING_MEDIUM",
  "BURNING_HIGH"
};

unsigned long long condition_bit_from_name(const char* name) {
  if (!name) return 0ULL;
  for (unsigned int i = 0; i < KNOWN_CONDITIONS_COUNT; i++) {
    if (KNOWN_CONDITIONS[i] && strcmp(KNOWN_CONDITIONS[i], name) == 0) {
      if (i < 64) return (1ULL << i);
      return 0ULL;
    }
  }
  return 0ULL;
}

const char* condition_name_from_index(unsigned int idx) {
  if (idx >= KNOWN_CONDITIONS_COUNT) return NULL;
  return KNOWN_CONDITIONS[idx];
}


