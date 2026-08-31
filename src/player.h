#ifndef VERSE_PLAYER_H
#define VERSE_PLAYER_H

#include <stdbool.h>

typedef struct {
  char current_world_id[64];  // ID of the current world
  float x;                   // Player X position
  float y;                   // Player Y position
  float z;                   // Player Z position
  unsigned int health;       // Player health
  unsigned int inventory_size;   // Size of inventory
  // Add more player stats as needed
} PlayerState;

// Function declarations
PlayerState* player_state_create(void);
void player_state_destroy(PlayerState* state);
bool save_player_state(PlayerState* state, const char* filename);
PlayerState* load_player_state(const char* filename);

#endif // VERSE_PLAYER_H 