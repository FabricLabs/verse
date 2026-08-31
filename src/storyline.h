#ifndef VERSE_STORYLINE_H
#define VERSE_STORYLINE_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

// A single step in a scene sequence (dialogue line or quest assignment).
typedef struct
{
    char *type; // "text" or "quest"
    char *content;
    char *quest_ref;
    char *quest_name;
    char *quest_description;
    int objectives_count;
    int completed_objectives;
} StorylineElement;

typedef struct
{
    char *id;
    char *name;
    char *description;
    char *world;
    StorylineElement *sequence;
    int sequence_count;
    int current_element;
    int current_char;
    int total_chars;
} StorylineScene;

#define STORYLINE_MAX_OBJECTIVES 8
#define STORYLINE_MAX_QUEST_HISTORY 32

typedef struct
{
    char *type;
    char *target;
    int required;
    int progress;
} StorylineObjective;

typedef struct
{
    char *id;
    char *name;
    char *description;
    char *world;
    StorylineObjective objectives[STORYLINE_MAX_OBJECTIVES];
    int objectives_count;
    int completed_objectives;
    bool is_active;
    bool is_completed;
} StorylineQuest;

typedef struct
{
    char *id;
    char *name;
    char *subtitle;
    char *description;
    char **scene_ids;
    int scene_count;
    int current_scene_index;

    // Multi-page chapter intro (built from chapter metadata + pages).
    int chapter_page;
    int chapter_page_count;
    char chapter_title[128];
    char chapter_page_text[2048];
    char player_name[64];

    StorylineScene *current_scene;
    StorylineQuest active_quest;
    // Completed quests in chronological order (oldest at [0], newest at the end).
    // The mission journal displays most-recent-first. Survives across activations.
    StorylineQuest quest_history[STORYLINE_MAX_QUEST_HISTORY];
    int quest_history_count;
    bool chapter_active;
    bool scene_active;
} StorylineState;

void storyline_init(StorylineState *state);
void storyline_destroy(StorylineState *state);

// Chapter flow (multi-page intro before the first scene).
bool storyline_start_chapter(StorylineState *state, const char *chapter_id, const char *player_name);
const char *storyline_chapter_title(const StorylineState *state);
const char *storyline_chapter_page_text(const StorylineState *state);
bool storyline_advance_chapter_page(StorylineState *state);
bool storyline_chapter_has_more_pages(const StorylineState *state);

// Scene flow (in-world dialogue after chapter intro).
bool storyline_start_scene(StorylineState *state, const char *scene_id);
bool storyline_scene_active(const StorylineState *state);
const char *storyline_scene_element_type(const StorylineState *state);
const char *storyline_scene_text(const StorylineState *state);
const char *storyline_scene_quest_name(const StorylineState *state);
const char *storyline_scene_quest_description(const StorylineState *state);
int storyline_scene_quest_objectives(const StorylineState *state);
int storyline_scene_quest_completed(const StorylineState *state);
void storyline_update_scene_streaming(StorylineState *state, Uint32 now_ms, int interval_ms);
bool storyline_scene_text_complete(const StorylineState *state);
bool storyline_advance_scene(StorylineState *state);

// Scene text streaming (single source of truth — StorylineScene counters).
int storyline_scene_current_char(const StorylineState *state);
int storyline_scene_total_chars(const StorylineState *state);
void storyline_scene_prepare_stream(StorylineState *state);
void storyline_scene_skip_to_end(StorylineState *state);
void storyline_scene_tick_char(StorylineState *state);

// Quest tracking (active quest assigned during a scene, or activated by world events).
bool storyline_has_active_quest(const StorylineState *state);
const StorylineQuest *storyline_active_quest(const StorylineState *state);
bool storyline_activate_quest(StorylineState *state, const char *quest_id);
void storyline_update_quest_progress(StorylineState *state, const char *objective_type, const char *target, int count);

// Completed quest log for the journal (storage oldest-first; journal UI shows newest first).
// Does not include the active quest.
int storyline_quest_history_count(const StorylineState *state);
const StorylineQuest *storyline_quest_history_entry(const StorylineState *state, int index);

#endif // VERSE_STORYLINE_H
