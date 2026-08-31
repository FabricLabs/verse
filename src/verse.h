#ifndef VERSE_H
#define VERSE_H

#include "engine.h"

typedef struct {
  char* event_id;
  char* description;
  // Add more fields as needed
} StorylineEvent;

void import_storyline_events();
void start_engine(Engine* engine);

#endif // VERSE_H 