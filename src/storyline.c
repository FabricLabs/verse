#include "storyline.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return NULL;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf)
    {
        fclose(file);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, file);
    buf[read] = '\0';
    fclose(file);
    return buf;
}

static char *dup_string(const json_t *obj, const char *key)
{
    json_t *val = json_object_get(obj, key);
    if (!val || !json_is_string(val))
        return NULL;
    return strdup(json_string_value(val));
}

static void free_scene(StorylineScene *scene)
{
    if (!scene)
        return;

    for (int i = 0; i < scene->sequence_count; i++)
    {
        free(scene->sequence[i].type);
        free(scene->sequence[i].content);
        free(scene->sequence[i].quest_ref);
        free(scene->sequence[i].quest_name);
        free(scene->sequence[i].quest_description);
    }

    free(scene->sequence);
    free(scene->id);
    free(scene->name);
    free(scene->description);
    free(scene->world);
    free(scene);
}


static void free_quest_objectives(StorylineQuest *quest)
{
    if (!quest)
        return;
    for (int i = 0; i < quest->objectives_count && i < STORYLINE_MAX_OBJECTIVES; i++)
    {
        free(quest->objectives[i].type);
        free(quest->objectives[i].target);
        quest->objectives[i].type = NULL;
        quest->objectives[i].target = NULL;
        quest->objectives[i].required = 0;
        quest->objectives[i].progress = 0;
    }
}

static void free_quest(StorylineQuest *quest)
{
    if (!quest)
        return;
    free_quest_objectives(quest);
    free(quest->id);
    free(quest->name);
    free(quest->description);
    free(quest->world);
    memset(quest, 0, sizeof(*quest));
}

static bool copy_quest(StorylineQuest *dst, const StorylineQuest *src)
{
    if (!dst || !src)
        return false;
    free_quest(dst);
    if (src->id)
        dst->id = strdup(src->id);
    if (src->name)
        dst->name = strdup(src->name);
    if (src->description)
        dst->description = strdup(src->description);
    if (src->world)
        dst->world = strdup(src->world);
    dst->objectives_count = src->objectives_count;
    if (dst->objectives_count > STORYLINE_MAX_OBJECTIVES)
        dst->objectives_count = STORYLINE_MAX_OBJECTIVES;
    for (int i = 0; i < dst->objectives_count; i++)
    {
        if (src->objectives[i].type)
            dst->objectives[i].type = strdup(src->objectives[i].type);
        if (src->objectives[i].target)
            dst->objectives[i].target = strdup(src->objectives[i].target);
        dst->objectives[i].required = src->objectives[i].required;
        dst->objectives[i].progress = src->objectives[i].progress;
    }
    dst->completed_objectives = src->completed_objectives;
    dst->is_active = src->is_active;
    dst->is_completed = src->is_completed;
    return true;
}

static bool quest_already_in_history(const StorylineState *state, const char *quest_id)
{
    if (!state || !quest_id)
        return false;
    for (int i = 0; i < state->quest_history_count; i++)
    {
        const char *id = state->quest_history[i].id;
        if (id && strcmp(id, quest_id) == 0)
            return true;
    }
    return false;
}

// Snapshot a completed (or replaced) quest into the journal log (appended = most recent).
static void archive_quest(StorylineState *state, const StorylineQuest *quest)
{
    if (!state || !quest || !quest->id || !quest->id[0])
        return;
    if (quest_already_in_history(state, quest->id))
        return;

    if (state->quest_history_count >= STORYLINE_MAX_QUEST_HISTORY)
    {
        free_quest(&state->quest_history[0]);
        memmove(&state->quest_history[0], &state->quest_history[1],
                sizeof(StorylineQuest) * (STORYLINE_MAX_QUEST_HISTORY - 1));
        state->quest_history_count = STORYLINE_MAX_QUEST_HISTORY - 1;
        memset(&state->quest_history[state->quest_history_count], 0, sizeof(StorylineQuest));
    }

    if (!copy_quest(&state->quest_history[state->quest_history_count], quest))
        return;
    state->quest_history[state->quest_history_count].is_active = false;
    state->quest_history_count++;
}

static StorylineQuest *load_quest(const char *quest_id)
{
    char path[256];
    snprintf(path, sizeof(path), "quests/%s.json", quest_id);

    char *json_text = read_file(path);
    if (!json_text)
    {
        printf("Storyline: failed to read quest file %s\n", path);
        return NULL;
    }

    json_error_t error;
    json_t *root = json_loads(json_text, 0, &error);
    free(json_text);
    if (!root)
    {
        printf("Storyline: failed to parse quest %s: %s\n", path, error.text);
        return NULL;
    }

    StorylineQuest *quest = calloc(1, sizeof(StorylineQuest));
    if (!quest)
    {
        json_decref(root);
        return NULL;
    }

    quest->id = strdup(quest_id);
    quest->name = dup_string(root, "name");
    quest->description = dup_string(root, "description");
    quest->world = dup_string(root, "world");

    json_t *objectives = json_object_get(root, "objectives");
    if (objectives && json_is_array(objectives))
    {
        const size_t n = json_array_size(objectives);
        quest->objectives_count = (int)((n > STORYLINE_MAX_OBJECTIVES) ? STORYLINE_MAX_OBJECTIVES : n);
        for (int i = 0; i < quest->objectives_count; i++)
        {
            json_t *obj = json_array_get(objectives, (size_t)i);
            if (!obj || !json_is_object(obj))
                continue;
            json_t *type = json_object_get(obj, "type");
            json_t *target = json_object_get(obj, "target");
            json_t *count = json_object_get(obj, "count");
            if (type && json_is_string(type))
                quest->objectives[i].type = strdup(json_string_value(type));
            if (target && json_is_string(target))
                quest->objectives[i].target = strdup(json_string_value(target));
            quest->objectives[i].required =
                (count && json_is_integer(count) && json_integer_value(count) > 0)
                    ? (int)json_integer_value(count)
                    : 1;
            quest->objectives[i].progress = 0;
        }
    }

    json_decref(root);
    return quest;
}

static StorylineScene *load_scene(const char *scene_id)
{
    char path[256];
    snprintf(path, sizeof(path), "scenes/%s.json", scene_id);

    char *json_text = read_file(path);
    if (!json_text)
    {
        printf("Storyline: failed to read scene file %s\n", path);
        return NULL;
    }

    json_error_t error;
    json_t *root = json_loads(json_text, 0, &error);
    free(json_text);
    if (!root)
    {
        printf("Storyline: failed to parse scene %s: %s\n", path, error.text);
        return NULL;
    }

    StorylineScene *scene = calloc(1, sizeof(StorylineScene));
    if (!scene)
    {
        json_decref(root);
        return NULL;
    }

    scene->id = strdup(scene_id);
    scene->name = dup_string(root, "name");
    scene->description = dup_string(root, "description");
    scene->world = dup_string(root, "world");

    json_t *seq = json_object_get(root, "sequence");
    if (seq && json_is_array(seq))
    {
        size_t count = json_array_size(seq);
        scene->sequence = calloc(count, sizeof(StorylineElement));
        scene->sequence_count = (int)count;

        for (size_t i = 0; i < count; i++)
        {
            json_t *element = json_array_get(seq, i);
            StorylineElement *elem = &scene->sequence[i];

            elem->type = dup_string(element, "type");
            elem->content = dup_string(element, "content");

            json_t *ref = json_object_get(element, "$ref");
            if (ref && json_is_string(ref))
                elem->quest_ref = strdup(json_string_value(ref));

            if (elem->type && strcmp(elem->type, "quest") == 0 && elem->quest_ref)
            {
                StorylineQuest *quest = load_quest(elem->quest_ref);
                if (quest)
                {
                    elem->quest_name = quest->name;
                    quest->name = NULL;
                    elem->quest_description = quest->description;
                    quest->description = NULL;
                    elem->objectives_count = quest->objectives_count;
                    free(quest->id);
                    free(quest->world);
                    free(quest);
                }
            }
        }
    }

    scene->current_element = 0;
    scene->current_char = 0;
    if (scene->sequence_count > 0 && scene->sequence[0].content)
        scene->total_chars = (int)strlen(scene->sequence[0].content);

    json_decref(root);
    printf("Storyline: loaded scene %s (%d elements)\n", scene_id, scene->sequence_count);
    return scene;
}

static bool load_chapter(StorylineState *state, const char *chapter_id)
{
    char path[256];
    snprintf(path, sizeof(path), "chapters/%s.json", chapter_id);

    char *json_text = read_file(path);
    if (!json_text)
    {
        printf("Storyline: failed to read chapter file %s\n", path);
        return false;
    }

    json_error_t error;
    json_t *root = json_loads(json_text, 0, &error);
    free(json_text);
    if (!root)
    {
        printf("Storyline: failed to parse chapter %s: %s\n", path, error.text);
        return false;
    }

    free(state->id);
    free(state->name);
    free(state->subtitle);
    free(state->description);

    state->id = dup_string(root, "name");
    if (!state->id)
        state->id = strdup(chapter_id);
    state->name = dup_string(root, "name");
    state->subtitle = dup_string(root, "subtitle");
    state->description = dup_string(root, "description");

    json_t *scenes = json_object_get(root, "scenes");
    if (scenes && json_is_array(scenes))
    {
        size_t count = json_array_size(scenes);
        state->scene_ids = calloc(count, sizeof(char *));
        state->scene_count = (int)count;
        for (size_t i = 0; i < count; i++)
        {
            json_t *scene_id = json_array_get(scenes, i);
            if (json_is_string(scene_id))
                state->scene_ids[i] = strdup(json_string_value(scene_id));
        }
    }

    if (state->name)
        snprintf(state->chapter_title, sizeof(state->chapter_title), "%s", state->name);
    else
        snprintf(state->chapter_title, sizeof(state->chapter_title), "Genesis");

    json_decref(root);
    return true;
}

static void build_chapter_page(StorylineState *state, int page)
{
    const char *name = state->player_name[0] ? state->player_name : "Traveler";

    switch (page)
    {
    case 0:
        snprintf(state->chapter_page_text, sizeof(state->chapter_page_text),
                 "In the beginning, there was only darkness.\n\n"
                 "Then, from the void, emerged the first spirits — "
                 "ethereal beings of pure consciousness, free from "
                 "the constraints of physical form.");
        break;
    case 1:
        snprintf(state->chapter_page_text, sizeof(state->chapter_page_text),
                 "You are one of these ancient spirits, awakened "
                 "to explore a vast overworld of floating islands.\n\n"
                 "Each island hangs above an infinite landscape below — "
                 "a tapestry of auto-generated worlds woven to match "
                 "the storyline of this realm.");
        break;
    case 2:
        snprintf(state->chapter_page_text, sizeof(state->chapter_page_text),
                 "Your journey begins on a personal island in the sky: "
                 "your Home World.\n\n"
                 "From here you will venture outward, discovering "
                 "neighboring worlds — some crafted by the universe itself, "
                 "others claimed by fellow travelers.");
        break;
    case 3:
        snprintf(state->chapter_page_text, sizeof(state->chapter_page_text),
                 "As a spirit, you are immortal but not invincible.\n\n"
                 "You may traverse worlds, inhabit entities, and shape "
                 "the fabric of existence — but danger lurks beyond "
                 "every horizon.");
        break;
    case 4:
        snprintf(state->chapter_page_text, sizeof(state->chapter_page_text),
                 "Your name is %s.\n\n"
                 "Awaken on your island. Listen to the wind. "
                 "Your destiny awaits...\n\n"
                 "Press ENTER to continue.",
                 name);
        break;
    default:
        state->chapter_page_text[0] = '\0';
        break;
    }
}

void storyline_init(StorylineState *state)
{
    if (!state)
        return;
    memset(state, 0, sizeof(*state));
    state->chapter_page_count = 5;
}

void storyline_destroy(StorylineState *state)
{
    if (!state)
        return;

    free(state->id);
    free(state->name);
    free(state->subtitle);
    free(state->description);

    for (int i = 0; i < state->scene_count; i++)
        free(state->scene_ids[i]);
    free(state->scene_ids);

    free_scene(state->current_scene);
    state->current_scene = NULL;

    free_quest(&state->active_quest);
    for (int i = 0; i < state->quest_history_count; i++)
        free_quest(&state->quest_history[i]);
    memset(state, 0, sizeof(*state));
}

bool storyline_start_chapter(StorylineState *state, const char *chapter_id, const char *player_name)
{
    if (!state || !chapter_id)
        return false;

    storyline_destroy(state);
    storyline_init(state);

    if (player_name)
        strncpy(state->player_name, player_name, sizeof(state->player_name) - 1);

    if (!load_chapter(state, chapter_id))
    {
        // Fallback metadata if JSON is missing.
        state->id = strdup(chapter_id);
        state->name = strdup("Genesis");
        snprintf(state->chapter_title, sizeof(state->chapter_title), "Genesis");
        state->scene_count = 1;
        state->scene_ids = calloc(1, sizeof(char *));
        state->scene_ids[0] = strdup("00000-genesis");
    }

    state->chapter_page = 0;
    state->chapter_active = true;
    state->scene_active = false;
    state->current_scene_index = 0;
    build_chapter_page(state, state->chapter_page);
    return true;
}

const char *storyline_chapter_title(const StorylineState *state)
{
    if (!state)
        return "Genesis";
    return state->chapter_title;
}

const char *storyline_chapter_page_text(const StorylineState *state)
{
    if (!state)
        return "";
    return state->chapter_page_text;
}

bool storyline_advance_chapter_page(StorylineState *state)
{
    if (!state || !state->chapter_active)
        return false;

    state->chapter_page++;
    if (state->chapter_page >= state->chapter_page_count)
    {
        state->chapter_active = false;
        return false;
    }

    build_chapter_page(state, state->chapter_page);
    return true;
}

bool storyline_chapter_has_more_pages(const StorylineState *state)
{
    if (!state || !state->chapter_active)
        return false;
    return (state->chapter_page + 1) < state->chapter_page_count;
}

bool storyline_start_scene(StorylineState *state, const char *scene_id)
{
    if (!state || !scene_id)
        return false;

    free_scene(state->current_scene);
    state->current_scene = load_scene(scene_id);
    if (!state->current_scene)
        return false;

    state->scene_active = true;
    state->current_scene->current_element = 0;
    state->current_scene->current_char = 0;
    if (state->current_scene->sequence_count > 0 &&
        state->current_scene->sequence[0].content)
    {
        state->current_scene->total_chars = (int)strlen(state->current_scene->sequence[0].content);
    }
    return true;
}

bool storyline_scene_active(const StorylineState *state)
{
    return state && state->scene_active && state->current_scene;
}

static StorylineElement *current_element(const StorylineState *state)
{
    if (!storyline_scene_active(state))
        return NULL;
    StorylineScene *scene = state->current_scene;
    if (scene->current_element < 0 || scene->current_element >= scene->sequence_count)
        return NULL;
    return &scene->sequence[scene->current_element];
}

const char *storyline_scene_element_type(const StorylineState *state)
{
    StorylineElement *elem = current_element(state);
    return elem ? elem->type : NULL;
}

const char *storyline_scene_text(const StorylineState *state)
{
    StorylineElement *elem = current_element(state);
    return (elem && elem->content) ? elem->content : "";
}

const char *storyline_scene_quest_name(const StorylineState *state)
{
    StorylineElement *elem = current_element(state);
    return (elem && elem->quest_name) ? elem->quest_name : "Quest";
}

const char *storyline_scene_quest_description(const StorylineState *state)
{
    StorylineElement *elem = current_element(state);
    return (elem && elem->quest_description) ? elem->quest_description : "";
}

int storyline_scene_quest_objectives(const StorylineState *state)
{
    StorylineElement *elem = current_element(state);
    return elem ? elem->objectives_count : 0;
}

int storyline_scene_quest_completed(const StorylineState *state)
{
    StorylineElement *elem = current_element(state);
    return elem ? elem->completed_objectives : 0;
}

void storyline_update_scene_streaming(StorylineState *state, Uint32 now_ms, int interval_ms)
{
    (void)now_ms;
    (void)interval_ms;
    // Streaming is driven by verse_client using chapter_last_text_update timing.
    (void)state;
}

bool storyline_scene_text_complete(const StorylineState *state)
{
    if (!storyline_scene_active(state))
        return true;

    StorylineScene *scene = state->current_scene;
    StorylineElement *elem = current_element(state);
    if (!elem)
        return true;

    if (elem->type && strcmp(elem->type, "quest") == 0)
        return true;

    if (!elem->content)
        return true;

    return scene->current_char >= scene->total_chars;
}

int storyline_scene_current_char(const StorylineState *state)
{
    if (!storyline_scene_active(state))
        return 0;
    return state->current_scene->current_char;
}

int storyline_scene_total_chars(const StorylineState *state)
{
    if (!storyline_scene_active(state))
        return 0;
    return state->current_scene->total_chars;
}

void storyline_scene_prepare_stream(StorylineState *state)
{
    if (!storyline_scene_active(state))
        return;

    StorylineScene *scene = state->current_scene;
    StorylineElement *elem = current_element(state);
    scene->current_char = 0;
    if (elem && elem->type && strcmp(elem->type, "text") == 0 && elem->content)
        scene->total_chars = (int)strlen(elem->content);
    else
        scene->total_chars = 0;
}

void storyline_scene_skip_to_end(StorylineState *state)
{
    if (!storyline_scene_active(state))
        return;
    state->current_scene->current_char = state->current_scene->total_chars;
}

void storyline_scene_tick_char(StorylineState *state)
{
    if (!storyline_scene_active(state))
        return;
    StorylineScene *scene = state->current_scene;
    if (scene->current_char < scene->total_chars)
        scene->current_char++;
}

bool storyline_activate_quest(StorylineState *state, const char *quest_id);

static void activate_quest_from_element(StorylineState *state, StorylineElement *elem)
{
    if (!state || !elem || !elem->quest_ref)
        return;
    (void)storyline_activate_quest(state, elem->quest_ref);
}

bool storyline_activate_quest(StorylineState *state, const char *quest_id)
{
    if (!state || !quest_id || !quest_id[0])
        return false;

    // Keep completed quests in the journal before loading the next one.
    if (state->active_quest.id && state->active_quest.is_completed)
        archive_quest(state, &state->active_quest);

    free_quest(&state->active_quest);

    StorylineQuest *loaded = load_quest(quest_id);
    if (!loaded)
        return false;

    state->active_quest = *loaded;
    state->active_quest.is_active = true;
    state->active_quest.is_completed = false;
    free(loaded);
    printf("Storyline: activated quest %s\n",
           state->active_quest.name ? state->active_quest.name : quest_id);
    return true;
}

bool storyline_advance_scene(StorylineState *state)
{
    if (!storyline_scene_active(state))
        return false;

    StorylineScene *scene = state->current_scene;
    StorylineElement *elem = current_element(state);

    if (elem && elem->type && strcmp(elem->type, "quest") == 0)
        activate_quest_from_element(state, elem);

    scene->current_element++;
    scene->current_char = 0;

    if (scene->current_element >= scene->sequence_count)
    {
        state->scene_active = false;
        free_scene(state->current_scene);
        state->current_scene = NULL;
        return false;
    }

    StorylineElement *next = &scene->sequence[scene->current_element];
    if (next->type && strcmp(next->type, "text") == 0 && next->content)
        scene->total_chars = (int)strlen(next->content);
    else
        scene->total_chars = 0;

    return true;
}

bool storyline_has_active_quest(const StorylineState *state)
{
    return state && state->active_quest.is_active && !state->active_quest.is_completed;
}

const StorylineQuest *storyline_active_quest(const StorylineState *state)
{
    if (!storyline_has_active_quest(state))
        return NULL;
    return &state->active_quest;
}

void storyline_update_quest_progress(StorylineState *state, const char *objective_type,
                                     const char *target, int count)
{
    if (!state || !storyline_has_active_quest(state) || count <= 0)
        return;

    StorylineQuest *quest = &state->active_quest;
    bool matched = false;
    for (int i = 0; i < quest->objectives_count && i < STORYLINE_MAX_OBJECTIVES; i++)
    {
        StorylineObjective *obj = &quest->objectives[i];
        if (obj->progress >= obj->required)
            continue;
        if (objective_type && obj->type && strcmp(obj->type, objective_type) != 0)
            continue;
        if (target && obj->target && strcmp(obj->target, target) != 0)
            continue;
        obj->progress += count;
        if (obj->progress > obj->required)
            obj->progress = obj->required;
        matched = true;
        break;
    }

    if (!matched && !objective_type && !target)
    {
        // Legacy callers with no type/target: advance the next incomplete objective.
        for (int i = 0; i < quest->objectives_count && i < STORYLINE_MAX_OBJECTIVES; i++)
        {
            StorylineObjective *obj = &quest->objectives[i];
            if (obj->progress >= obj->required)
                continue;
            obj->progress += count;
            if (obj->progress > obj->required)
                obj->progress = obj->required;
            matched = true;
            break;
        }
    }

    if (!matched)
        return;

    int done = 0;
    for (int i = 0; i < quest->objectives_count && i < STORYLINE_MAX_OBJECTIVES; i++)
        if (quest->objectives[i].progress >= quest->objectives[i].required)
            done++;
    quest->completed_objectives = done;

    if (quest->completed_objectives >= quest->objectives_count && quest->objectives_count > 0)
    {
        quest->completed_objectives = quest->objectives_count;
        quest->is_completed = true;
        quest->is_active = false;
        archive_quest(state, quest);
        printf("Storyline: quest completed — %s\n",
               quest->name ? quest->name : "unknown");
    }
}

int storyline_quest_history_count(const StorylineState *state)
{
    return state ? state->quest_history_count : 0;
}

const StorylineQuest *storyline_quest_history_entry(const StorylineState *state, int index)
{
    if (!state || index < 0 || index >= state->quest_history_count)
        return NULL;
    return &state->quest_history[index];
}
