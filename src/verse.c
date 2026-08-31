#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include "verse.h"
#include "engine.h"
#include "constants.h"

void import_storyline_events () {
  // Storyline content is loaded on demand by src/storyline.c when a new game
  // begins (chapters/, scenes/, quests/ JSON). This hook remains for future
  // server-side event import.
  printf("Storyline system ready (JSON chapters/scenes/quests)\n");
}

void start_engine(Engine* engine) {
    if (!engine) return;

    // Initialize engine state
    printf("Starting engine...\n");

    // Load any saved actors
    engine_load_actors(engine, "actors.save");

    printf("Engine started successfully\n");
}

// Removed main function