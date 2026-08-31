#include "dialogue.h"
#include "mob_ai.h"
#include "household.h"
#include "chronicle.h"
#include "world.h"

#include <stdio.h>
#include <string.h>

static DialogueEffect s_last_effect = DIALOGUE_EFFECT_NONE;

static int dialogue_add_line(char out_lines[][DIALOGUE_LINE_MAX], int max_lines, int n,
                             const char *text)
{
    if (n < 0 || n >= max_lines || !out_lines || !text)
        return n;
    strncpy(out_lines[n], text, DIALOGUE_LINE_MAX - 1);
    out_lines[n][DIALOGUE_LINE_MAX - 1] = '\0';
    return n + 1;
}

static bool actor_is_mud_golem(const Actor *actor)
{
    if (!actor)
        return false;
    if (actor->name[0] && strcmp(actor->name, "Mud Golem") == 0)
        return true;
    if (strstr(actor->description, "clay") || strstr(actor->description, "wet clay"))
        return true;
    return false;
}

static void dialogue_load_node_lines(DialogueState *d)
{
    if (!d || d->current_node < 0 || d->current_node >= d->node_count)
        return;
    const DialogueNode *node = &d->nodes[d->current_node];
    d->line_count = node->line_count;
    for (int i = 0; i < node->line_count && i < DIALOGUE_MAX_LINES; i++)
    {
        strncpy(d->lines[i], node->lines[i], DIALOGUE_LINE_MAX - 1);
        d->lines[i][DIALOGUE_LINE_MAX - 1] = '\0';
    }
    d->current_line = 0;
    d->current_char = 0;
    d->last_char_ms = 0;
    d->phase = DIALOGUE_PHASE_SPEAKING;
    d->selected_choice = 0;
}

static int dialogue_add_node_line(DialogueNode *node, const char *text)
{
    if (!node || node->line_count >= DIALOGUE_MAX_LINES || !text)
        return node ? node->line_count : 0;
    strncpy(node->lines[node->line_count], text, DIALOGUE_LINE_MAX - 1);
    node->lines[node->line_count][DIALOGUE_LINE_MAX - 1] = '\0';
    node->line_count++;
    return node->line_count;
}

static void dialogue_add_choice(DialogueNode *node, const char *label, int next_node,
                                DialogueEffect effect)
{
    if (!node || node->choice_count >= DIALOGUE_MAX_CHOICES || !label)
        return;
    DialogueChoice *c = &node->choices[node->choice_count++];
    strncpy(c->label, label, DIALOGUE_CHOICE_LABEL_MAX - 1);
    c->label[DIALOGUE_CHOICE_LABEL_MAX - 1] = '\0';
    c->next_node = (int8_t)next_node;
    c->effect = effect;
}

void dialogue_init(DialogueState *d)
{
    if (!d)
        return;
    memset(d, 0, sizeof(*d));
    d->notified_anim_line = -1;
    d->phase = DIALOGUE_PHASE_CLOSED;
    d->current_node = -1;
}

void dialogue_close(DialogueState *d)
{
    if (!d)
        return;
    d->active = false;
    d->phase = DIALOGUE_PHASE_CLOSED;
    d->speaker[0] = '\0';
    d->line_count = 0;
    d->current_line = 0;
    d->current_char = 0;
    d->speaker_id = 0;
    d->last_char_ms = 0;
    d->notified_anim_line = -1;
    d->node_count = 0;
    d->current_node = -1;
    d->selected_choice = 0;
    d->has_graph = false;
}

bool dialogue_begin(DialogueState *d, uint32_t speaker_id, const char *speaker,
                    const char *const *lines, int line_count)
{
    if (!d || !lines || line_count <= 0)
        return false;

    dialogue_init(d);
    d->speaker_id = speaker_id;
    if (speaker && speaker[0])
        strncpy(d->speaker, speaker, DIALOGUE_SPEAKER_MAX - 1);
    else
        strncpy(d->speaker, "Someone", DIALOGUE_SPEAKER_MAX - 1);
    d->speaker[DIALOGUE_SPEAKER_MAX - 1] = '\0';

    int n = line_count;
    if (n > DIALOGUE_MAX_LINES)
        n = DIALOGUE_MAX_LINES;
    for (int i = 0; i < n; i++)
    {
        const char *line = lines[i] ? lines[i] : "...";
        strncpy(d->lines[i], line, DIALOGUE_LINE_MAX - 1);
        d->lines[i][DIALOGUE_LINE_MAX - 1] = '\0';
    }
    d->line_count = n;
    d->current_line = 0;
    d->current_char = 0;
    d->last_char_ms = 0;
    d->active = true;
    d->phase = DIALOGUE_PHASE_SPEAKING;
    d->has_graph = false;

    // Single Leave choice after monologue for non-graph talks.
    DialogueNode *node = &d->nodes[0];
    memset(node, 0, sizeof(*node));
    for (int i = 0; i < n; i++)
        dialogue_add_node_line(node, d->lines[i]);
    dialogue_add_choice(node, "Leave", -1, DIALOGUE_EFFECT_CLOSE);
    d->node_count = 1;
    d->current_node = 0;
    d->has_graph = true;
    return true;
}

static bool build_villager_graph(DialogueState *d, const Actor *actor, World *world,
                                 const Chronicle *chronicle)
{
    const MobActor *vm = actor->extra_data ? (const MobActor *)actor->extra_data : NULL;
    if (!vm || vm->mob_type != MOB_TYPE_VILLAGER)
        return false;

    VillagerKind vk = vm->villager_kind;
    VillagerProfession job = vm->profession;
    const char *job_name = villager_profession_name(job);
    char line[DIALOGUE_LINE_MAX];

    // Node 0: greeting + root choices
    // Node 1: about self
    // Node 2: about family
    // Node 3: about town
    // Node 4: news
    // Node 5: goodbye (unused — close)
    for (int i = 0; i < 5; i++)
        memset(&d->nodes[i], 0, sizeof(d->nodes[i]));

    DialogueNode *root = &d->nodes[0];
    if (vk == VILLAGER_CHILD)
        dialogue_add_node_line(root, "Hi!");
    else
    {
        snprintf(line, sizeof(line), "Fair day. I am %s.",
                 actor->name[0] ? actor->name : "a villager");
        dialogue_add_node_line(root, line);
    }
    dialogue_add_choice(root, "Ask about themselves", 1, DIALOGUE_EFFECT_NONE);
    dialogue_add_choice(root, "Ask about their family", 2, DIALOGUE_EFFECT_NONE);
    dialogue_add_choice(root, "Ask about the town", 3, DIALOGUE_EFFECT_NONE);
    dialogue_add_choice(root, "Heard any news?", 4, DIALOGUE_EFFECT_NONE);
    if (vk != VILLAGER_CHILD && job == VILLAGER_JOB_MERCHANT)
        dialogue_add_choice(root, "Browse the shop", -1, DIALOGUE_EFFECT_OPEN_SHOP);
    dialogue_add_choice(root, "Goodbye", -1, DIALOGUE_EFFECT_CLOSE);

    DialogueNode *self = &d->nodes[1];
    if (vk == VILLAGER_CHILD)
    {
        dialogue_add_node_line(self, "I'm still learning my trade.");
        if (vm->family_name[0])
        {
            snprintf(line, sizeof(line), "I'm with the %ss.", vm->family_name);
            dialogue_add_node_line(self, line);
        }
    }
    else
    {
        snprintf(line, sizeof(line), "I work as %s here.", job_name);
        dialogue_add_node_line(self, line);
        if (job == VILLAGER_JOB_MERCHANT)
        {
            dialogue_add_node_line(self, "I keep the shop — goods from myself and the villagers.");
            dialogue_add_choice(self, "Browse the shop", -1, DIALOGUE_EFFECT_OPEN_SHOP);
        }
        else if (vm->trait_curiosity > 40)
            dialogue_add_node_line(self, "I like hearing travelers' tales.");
        else if (vm->trait_wrath > 40)
            dialogue_add_node_line(self, "Don't make trouble in our streets.");
        else
            dialogue_add_node_line(self, "Life is quiet when the flocks behave.");
    }
    dialogue_add_choice(self, "Back", 0, DIALOGUE_EFFECT_NONE);
    dialogue_add_choice(self, "Goodbye", -1, DIALOGUE_EFFECT_CLOSE);

    DialogueNode *fam = &d->nodes[2];
    if (vm->family_name[0])
    {
        snprintf(line, sizeof(line), "We are the %s family.", vm->family_name);
        dialogue_add_node_line(fam, line);
    }
    else
        dialogue_add_node_line(fam, "I keep to myself.");

    if (world && vm->family_id)
    {
        uint32_t ids[HOUSEHOLD_MAX_MEMBERS];
        int nm = household_members(world, vm->family_id, ids, HOUSEHOLD_MAX_MEMBERS);
        int named = 0;
        for (int i = 0; i < nm && named < 3; i++)
        {
            if (ids[i] == actor->id)
                continue;
            Actor *kin = world_find_runtime_actor(world, ids[i]);
            if (!kin || !kin->name[0])
                continue;
            snprintf(line, sizeof(line), "Kin: %s.", kin->name);
            dialogue_add_node_line(fam, line);
            named++;
        }
        if (named == 0)
            dialogue_add_node_line(fam, "Most of us are out working.");
    }
    dialogue_add_choice(fam, "Back", 0, DIALOGUE_EFFECT_NONE);
    dialogue_add_choice(fam, "Mark family on map", 0, DIALOGUE_EFFECT_NAV_FAMILY);
    dialogue_add_choice(fam, "Goodbye", -1, DIALOGUE_EFFECT_CLOSE);

    DialogueNode *town = &d->nodes[3];
    const SettlementRoster *roster = world ? world_settlement_roster_const(world) : NULL;
    if (roster && roster->active && roster->name[0])
    {
        snprintf(line, sizeof(line), "This is %s.", roster->name);
        dialogue_add_node_line(town, line);
        if (chronicle)
        {
            const ChronicleCiv *civ = chronicle_civ(chronicle, roster->civ_id);
            if (civ)
            {
                snprintf(line, sizeof(line), "We stand with %s.", civ->name);
                dialogue_add_node_line(town, line);
                if (civ->deity[0])
                {
                    snprintf(line, sizeof(line), "Our folk honor %s.", civ->deity);
                    dialogue_add_node_line(town, line);
                }
            }
        }
        snprintf(line, sizeof(line), "%u families live here.", (unsigned)roster->family_count);
        dialogue_add_node_line(town, line);
    }
    else
        dialogue_add_node_line(town, "Just a quiet place on the road.");
    dialogue_add_choice(town, "Back", 0, DIALOGUE_EFFECT_NONE);
    dialogue_add_choice(town, "Mark the town", 0, DIALOGUE_EFFECT_NAV_SETTLEMENT);
    dialogue_add_choice(town, "Goodbye", -1, DIALOGUE_EFFECT_CLOSE);

    DialogueNode *news = &d->nodes[4];
    bool told = false;
    if (vm->story_count > 0)
    {
        for (uint8_t i = 0; i < vm->story_count && i < 2; i++)
        {
            dialogue_add_node_line(news, vm->stories[i].text);
            told = true;
        }
    }
    if (chronicle && world)
    {
        const ChronicleSite *site = chronicle_site_at(chronicle, (int)world->universe_x,
                                                     (int)world->universe_y);
        if (site)
        {
            const ChronicleEvent *evs[4];
            int ne = chronicle_query_by_site(chronicle, site->id, evs, 4);
            char buf[CHRONICLE_LINE_MAX];
            for (int i = ne - 1; i >= 0 && !told; i--)
            {
                if (evs[i]->type == CHRONICLE_EV_SITE_FOUND ||
                    evs[i]->type == CHRONICLE_EV_CIV_FOUND)
                    continue;
                chronicle_format_line(chronicle, evs[i], buf, sizeof(buf));
                if (buf[0])
                {
                    snprintf(line, sizeof(line), "Heard tell: %s", buf);
                    dialogue_add_node_line(news, line);
                    told = true;
                }
            }
        }
    }
    if (!told)
        dialogue_add_node_line(news, "Nothing new under the sky.");
    dialogue_add_choice(news, "Back", 0, DIALOGUE_EFFECT_NONE);
    dialogue_add_choice(news, "Show legends", 0, DIALOGUE_EFFECT_OPEN_LEGENDS);
    dialogue_add_choice(news, "Goodbye", -1, DIALOGUE_EFFECT_CLOSE);

    d->node_count = 5;
    d->current_node = 0;
    d->has_graph = true;
    dialogue_load_node_lines(d);
    return true;
}

bool dialogue_begin_conversation(DialogueState *d, const Actor *actor, struct World *world,
                                 const Chronicle *chronicle)
{
    if (!d || !actor)
        return false;

    dialogue_init(d);
    d->speaker_id = actor->id;
    strncpy(d->speaker, actor->name[0] ? actor->name : "Someone", DIALOGUE_SPEAKER_MAX - 1);
    d->speaker[DIALOGUE_SPEAKER_MAX - 1] = '\0';
    d->active = true;
    s_last_effect = DIALOGUE_EFFECT_NONE;

    if (mob_actor_is_villager(actor) && build_villager_graph(d, actor, world, chronicle))
        return true;

    // Non-villagers: linear lines + Leave.
    char stored[DIALOGUE_MAX_LINES][DIALOGUE_LINE_MAX];
    int n = dialogue_lines_for_actor(actor, stored, DIALOGUE_MAX_LINES);
    const char *ptrs[DIALOGUE_MAX_LINES];
    for (int i = 0; i < n; i++)
        ptrs[i] = stored[i];
    return dialogue_begin(d, actor->id, d->speaker, ptrs, n > 0 ? n : 1);
}

void dialogue_tick(DialogueState *d, uint32_t now_ms)
{
    if (!d || !d->active || d->phase != DIALOGUE_PHASE_SPEAKING)
        return;

    const int len = dialogue_current_line_length(d);
    if (d->current_char >= len)
        return;

    if (d->last_char_ms == 0)
    {
        d->last_char_ms = now_ms;
        if (len > 0)
            d->current_char = 1;
        return;
    }

    while (d->current_char < len &&
           (int32_t)(now_ms - d->last_char_ms) >= (int32_t)DIALOGUE_CHAR_INTERVAL_MS)
    {
        d->current_char++;
        d->last_char_ms += DIALOGUE_CHAR_INTERVAL_MS;
    }
}

bool dialogue_advance(DialogueState *d)
{
    if (!d || !d->active)
        return false;

    if (d->phase == DIALOGUE_PHASE_AWAITING_CHOICE)
        return dialogue_choice_confirm(d);

    const int len = dialogue_current_line_length(d);
    if (d->current_char < len)
    {
        d->current_char = len;
        return true;
    }

    d->current_line++;
    d->current_char = 0;
    d->last_char_ms = 0;
    if (d->current_line >= d->line_count)
    {
        if (d->has_graph && d->current_node >= 0 && d->current_node < d->node_count &&
            d->nodes[d->current_node].choice_count > 0)
        {
            d->phase = DIALOGUE_PHASE_AWAITING_CHOICE;
            d->selected_choice = 0;
            return true;
        }
        dialogue_close(d);
        return false;
    }
    return true;
}

void dialogue_choice_move(DialogueState *d, int delta)
{
    if (!d || !d->active || d->phase != DIALOGUE_PHASE_AWAITING_CHOICE)
        return;
    int count = dialogue_choice_count(d);
    if (count <= 0)
        return;
    int sel = d->selected_choice + delta;
    while (sel < 0)
        sel += count;
    while (sel >= count)
        sel -= count;
    d->selected_choice = sel;
}

bool dialogue_choice_confirm(DialogueState *d)
{
    if (!d || !d->active || d->phase != DIALOGUE_PHASE_AWAITING_CHOICE)
        return false;
    if (d->current_node < 0 || d->current_node >= d->node_count)
    {
        dialogue_close(d);
        return false;
    }
    const DialogueNode *node = &d->nodes[d->current_node];
    if (d->selected_choice < 0 || d->selected_choice >= node->choice_count)
    {
        dialogue_close(d);
        return false;
    }
    const DialogueChoice *ch = &node->choices[d->selected_choice];
    s_last_effect = ch->effect;
    if (ch->effect == DIALOGUE_EFFECT_CLOSE || ch->next_node < 0)
    {
        dialogue_close(d);
        return false;
    }
    if (ch->next_node >= d->node_count)
    {
        dialogue_close(d);
        return false;
    }
    d->current_node = ch->next_node;
    dialogue_load_node_lines(d);
    return true;
}

int dialogue_choice_count(const DialogueState *d)
{
    if (!d || !d->active || d->current_node < 0 || d->current_node >= d->node_count)
        return 0;
    return d->nodes[d->current_node].choice_count;
}

int dialogue_selected_choice(const DialogueState *d)
{
    return d ? d->selected_choice : 0;
}

const char *dialogue_choice_label(const DialogueState *d, int index)
{
    if (!d || d->current_node < 0 || d->current_node >= d->node_count)
        return "";
    const DialogueNode *node = &d->nodes[d->current_node];
    if (index < 0 || index >= node->choice_count)
        return "";
    return node->choices[index].label;
}

DialogueEffect dialogue_last_effect(const DialogueState *d)
{
    (void)d;
    return s_last_effect;
}

bool dialogue_active(const DialogueState *d)
{
    return d && d->active;
}

bool dialogue_awaiting_choice(const DialogueState *d)
{
    return d && d->active && d->phase == DIALOGUE_PHASE_AWAITING_CHOICE;
}

const char *dialogue_speaker(const DialogueState *d)
{
    if (!d || !d->active)
        return "";
    return d->speaker;
}

const char *dialogue_current_text(const DialogueState *d)
{
    if (!d || !d->active || d->current_line < 0 || d->current_line >= d->line_count)
        return "";
    return d->lines[d->current_line];
}

int dialogue_current_char(const DialogueState *d)
{
    if (!d || !d->active)
        return 0;
    return d->current_char;
}

int dialogue_current_line_length(const DialogueState *d)
{
    const char *text = dialogue_current_text(d);
    return (int)strlen(text);
}

int dialogue_line_index(const DialogueState *d)
{
    if (!d || !d->active)
        return 0;
    return d->current_line;
}

int dialogue_line_count(const DialogueState *d)
{
    if (!d || !d->active)
        return 0;
    return d->line_count;
}

uint32_t dialogue_speaker_id(const DialogueState *d)
{
    if (!d || !d->active)
        return 0;
    return d->speaker_id;
}

bool dialogue_line_complete(const DialogueState *d)
{
    if (!d || !d->active)
        return true;
    return d->current_char >= dialogue_current_line_length(d);
}

int dialogue_lines_for_actor(const Actor *actor, char out_lines[][DIALOGUE_LINE_MAX], int max_lines)
{
    if (!out_lines || max_lines <= 0)
        return 0;

    if (!actor)
        return dialogue_add_line(out_lines, max_lines, 0, "...");

    if (actor_is_mud_golem(actor))
    {
        int n = 0;
        n = dialogue_add_line(out_lines, max_lines, n, "...");
        n = dialogue_add_line(out_lines, max_lines, n, "Schmmmmufff...");
        return n;
    }

    MobType type = MOB_TYPE_NONE;
    if (actor->extra_data)
        type = ((const MobActor *)actor->extra_data)->mob_type;

    int n = 0;
    switch (type)
    {
    case MOB_TYPE_WANDERER:
        n = dialogue_add_line(out_lines, max_lines, n, "...");
        n = dialogue_add_line(out_lines, max_lines, n, "*shuffles*");
        break;
    case MOB_TYPE_SOLVER:
        n = dialogue_add_line(out_lines, max_lines, n, "...");
        n = dialogue_add_line(out_lines, max_lines, n, "The way out...");
        break;
    case MOB_TYPE_GUARD:
        n = dialogue_add_line(out_lines, max_lines, n, "Halt.");
        n = dialogue_add_line(out_lines, max_lines, n, "...");
        break;
    case MOB_TYPE_HUNTER:
        n = dialogue_add_line(out_lines, max_lines, n, "*snarl*");
        break;
    case MOB_TYPE_BUILDER:
        n = dialogue_add_line(out_lines, max_lines, n, "*tap*");
        break;
    case MOB_TYPE_DIGGER:
        n = dialogue_add_line(out_lines, max_lines, n, "*scrape*");
        break;
    case MOB_TYPE_BIRD:
        n = dialogue_add_line(out_lines, max_lines, n, "*chirp*");
        break;
    case MOB_TYPE_SHEEP:
        n = dialogue_add_line(out_lines, max_lines, n, "Baa.");
        break;
    case MOB_TYPE_CHICKEN:
        n = dialogue_add_line(out_lines, max_lines, n, "*cluck*");
        break;
    case MOB_TYPE_BAT:
        n = dialogue_add_line(out_lines, max_lines, n, "*squeak*");
        break;
    case MOB_TYPE_DEER:
        n = dialogue_add_line(out_lines, max_lines, n, "*snort*");
        break;
    case MOB_TYPE_LIZARD:
        n = dialogue_add_line(out_lines, max_lines, n, "*hiss*");
        break;
    case MOB_TYPE_SPIDER:
        n = dialogue_add_line(out_lines, max_lines, n, "*skitter*");
        break;
    case MOB_TYPE_SLIME:
        n = dialogue_add_line(out_lines, max_lines, n, "*blorp*");
        break;
    case MOB_TYPE_VILLAGER:
    {
        const MobActor *vm = actor->extra_data ? (const MobActor *)actor->extra_data : NULL;
        VillagerKind vk = mob_actor_villager_kind(actor);
        VillagerProfession job = mob_actor_villager_profession(actor);
        const char *job_name = villager_profession_name(job);
        char line[DIALOGUE_LINE_MAX];
        if (vk == VILLAGER_CHILD)
        {
            n = dialogue_add_line(out_lines, max_lines, n, "Hi!");
            if (vm && vm->family_name[0])
            {
                snprintf(line, sizeof(line), "I'm with the %ss.", vm->family_name);
                n = dialogue_add_line(out_lines, max_lines, n, line);
            }
        }
        else
        {
            snprintf(line, sizeof(line), "Fair day. I work as %s here.", job_name);
            n = dialogue_add_line(out_lines, max_lines, n, line);
        }
        if (vm && vm->story_count > 0 && n < max_lines)
            n = dialogue_add_line(out_lines, max_lines, n, vm->stories[0].text);
        break;
    }
    case MOB_TYPE_NONE:
    default:
        n = dialogue_add_line(out_lines, max_lines, n, "...");
        break;
    }
    return n;
}
