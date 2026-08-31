#ifndef VERSE_DIALOGUE_H
#define VERSE_DIALOGUE_H

#include <stdbool.h>
#include <stdint.h>

#include "actor.h"

#define DIALOGUE_MAX_LINES 8
#define DIALOGUE_SPEAKER_MAX 64
#define DIALOGUE_LINE_MAX 192
#define DIALOGUE_CHAR_INTERVAL_MS 40u
#define DIALOGUE_MAX_NODES 16
#define DIALOGUE_MAX_CHOICES 6
#define DIALOGUE_CHOICE_LABEL_MAX 48

typedef enum {
  DIALOGUE_PHASE_SPEAKING = 0,
  DIALOGUE_PHASE_AWAITING_CHOICE,
  DIALOGUE_PHASE_CLOSED
} DialoguePhase;

typedef enum {
  DIALOGUE_EFFECT_NONE = 0,
  DIALOGUE_EFFECT_CLOSE,
  DIALOGUE_EFFECT_REPUTATION,
  DIALOGUE_EFFECT_NAV_FAMILY,
  DIALOGUE_EFFECT_NAV_SETTLEMENT,
  DIALOGUE_EFFECT_OPEN_LEGENDS,
  DIALOGUE_EFFECT_OPEN_SHOP
} DialogueEffect;

typedef struct {
  char label[DIALOGUE_CHOICE_LABEL_MAX];
  int8_t next_node; // -1 = close after effect
  DialogueEffect effect;
} DialogueChoice;

typedef struct {
  char lines[DIALOGUE_MAX_LINES][DIALOGUE_LINE_MAX];
  int line_count;
  DialogueChoice choices[DIALOGUE_MAX_CHOICES];
  int choice_count;
} DialogueNode;

// Live NPC/mob conversation, drawn over the world rather than replacing it.
typedef struct DialogueState
{
    bool active;
    DialoguePhase phase;
    char speaker[DIALOGUE_SPEAKER_MAX];
    // Linear fallback buffer (also used as current node line display)
    char lines[DIALOGUE_MAX_LINES][DIALOGUE_LINE_MAX];
    int line_count;
    int current_line;
    int current_char;
    uint32_t speaker_id;
    uint32_t last_char_ms;
    int notified_anim_line; // last line that triggered Yes/No; -1 none

    // Branching conversation graph
    DialogueNode nodes[DIALOGUE_MAX_NODES];
    int node_count;
    int current_node;
    int selected_choice;
    bool has_graph;
} DialogueState;

struct World;
struct Chronicle;

void dialogue_init(DialogueState *d);
void dialogue_close(DialogueState *d);

// Linear monologue (non-villager / portal prompts).
bool dialogue_begin(DialogueState *d, uint32_t speaker_id, const char *speaker,
                    const char *const *lines, int line_count);

// Build villager (or generic) branching conversation for actor in world.
bool dialogue_begin_conversation(DialogueState *d, const Actor *actor, struct World *world,
                                 const struct Chronicle *chronicle);

void dialogue_tick(DialogueState *d, uint32_t now_ms);
// Completes the current line, advances to the next, enters choice mode, or closes.
bool dialogue_advance(DialogueState *d);

// Choice navigation while awaiting_choice.
void dialogue_choice_move(DialogueState *d, int delta);
bool dialogue_choice_confirm(DialogueState *d);
int dialogue_choice_count(const DialogueState *d);
int dialogue_selected_choice(const DialogueState *d);
const char *dialogue_choice_label(const DialogueState *d, int index);
DialogueEffect dialogue_last_effect(const DialogueState *d);

bool dialogue_active(const DialogueState *d);
bool dialogue_awaiting_choice(const DialogueState *d);
const char *dialogue_speaker(const DialogueState *d);
const char *dialogue_current_text(const DialogueState *d);
int dialogue_current_char(const DialogueState *d);
int dialogue_current_line_length(const DialogueState *d);
int dialogue_line_index(const DialogueState *d);
int dialogue_line_count(const DialogueState *d);
uint32_t dialogue_speaker_id(const DialogueState *d);
bool dialogue_line_complete(const DialogueState *d);

// Context-aware lines for a runtime actor. Always writes at least one line. Returns the count.
int dialogue_lines_for_actor(const Actor *actor, char out_lines[][DIALOGUE_LINE_MAX], int max_lines);

#endif
