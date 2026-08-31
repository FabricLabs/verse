#include "chronicle.h"

#include "settlement.h"
#include "universe_biome.h"

#include <stdio.h>
#include <string.h>

static uint32_t chron_hash(uint32_t x)
{
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

static uint32_t chron_seed_u32(const char *seed_hex)
{
  uint32_t h = 0xC4A11C1Eu;
  if (!seed_hex)
    return h;
  for (const unsigned char *p = (const unsigned char *)seed_hex; *p; p++)
    h = chron_hash(h ^ *p);
  return h ? h : 1u;
}

static uint32_t chron_rng(uint32_t *state)
{
  *state = chron_hash(*state + 0x9E3779B9u);
  return *state;
}

static float chron_rng_f(uint32_t *state)
{
  return (float)(chron_rng(state) & 0xFFFFFFu) / (float)0x1000000u;
}

void chronicle_init(Chronicle *c)
{
  if (!c)
    return;
  memset(c, 0, sizeof(*c));
}

void chronicle_clear(Chronicle *c)
{
  chronicle_init(c);
}

uint32_t chronicle_site_id_for_cell(int gx, int gy)
{
  if (universe_settlement_scale(gx, gy) <= 0)
    return 0;
  uint32_t h = chron_hash((uint32_t)gx * 374761393u ^ (uint32_t)gy * 668265263u ^ 0x51EEu);
  return h ? h : 1u;
}

const char *chronicle_event_type_name(ChronicleEventType t)
{
  static const char *names[] = {
      "none", "civ_found", "site_found", "site_abandon", "war_raid", "peace",
      "migration", "plague", "beast_attack", "artifact_created", "figure_rise",
      "figure_death", "family_found", "trade"};
  if (t < 0 || t >= CHRONICLE_EV_COUNT)
    return "unknown";
  return names[t];
}

const char *chronicle_site_kind_name(ChronicleSiteKind k)
{
  static const char *names[] = {"hamlet", "town", "fort", "ruin", "lair", "shrine"};
  if (k < 0 || k >= CHRONICLE_SITE_KIND_COUNT)
    return "site";
  return names[k];
}

const ChronicleCiv *chronicle_civ(const Chronicle *c, uint32_t civ_id)
{
  if (!c || !civ_id)
    return NULL;
  for (uint32_t i = 0; i < c->civ_count; i++)
    if (c->civs[i].id == civ_id)
      return &c->civs[i];
  return NULL;
}

const ChronicleSite *chronicle_site(const Chronicle *c, uint32_t site_id)
{
  if (!c || !site_id)
    return NULL;
  for (uint32_t i = 0; i < c->site_count; i++)
    if (c->sites[i].id == site_id)
      return &c->sites[i];
  return NULL;
}

const ChronicleSite *chronicle_site_at(const Chronicle *c, int gx, int gy)
{
  if (!c)
    return NULL;
  for (uint32_t i = 0; i < c->site_count; i++)
    if (c->sites[i].gx == gx && c->sites[i].gy == gy)
      return &c->sites[i];
  return NULL;
}

const ChronicleArtifact *chronicle_artifact(const Chronicle *c, uint32_t artifact_id)
{
  if (!c || !artifact_id)
    return NULL;
  for (uint32_t i = 0; i < c->artifact_count; i++)
    if (c->artifacts[i].id == artifact_id)
      return &c->artifacts[i];
  return NULL;
}

const ChronicleFigure *chronicle_figure(const Chronicle *c, uint32_t figure_id)
{
  if (!c || !figure_id)
    return NULL;
  for (uint32_t i = 0; i < c->figure_count; i++)
    if (c->figures[i].id == figure_id)
      return &c->figures[i];
  return NULL;
}

static bool chron_push_event(Chronicle *c, ChronicleEventType type, uint16_t year, uint32_t civ_a,
                             uint32_t civ_b, uint32_t site_id, uint32_t figure_id,
                             uint32_t artifact_id, uint32_t family_id, int16_t param)
{
  if (!c || c->event_count >= CHRONICLE_MAX_EVENTS)
    return false;
  ChronicleEvent *e = &c->events[c->event_count++];
  e->type = type;
  e->year = year;
  e->civ_a = civ_a;
  e->civ_b = civ_b;
  e->site_id = site_id;
  e->figure_id = figure_id;
  e->artifact_id = artifact_id;
  e->family_id = family_id;
  e->param = param;
  return true;
}

static ChronicleSiteKind kind_from_scale(int scale, uint32_t roll, bool ruin)
{
  if (ruin)
    return CHRONICLE_SITE_RUIN;
  if ((roll % 40u) == 0)
    return CHRONICLE_SITE_SHRINE;
  if ((roll % 55u) == 0)
    return CHRONICLE_SITE_LAIR;
  if (scale >= 7)
    return CHRONICLE_SITE_FORT;
  if (scale >= 5)
    return CHRONICLE_SITE_TOWN;
  return CHRONICLE_SITE_HAMLET;
}

static void pick_site_name(char *out, size_t n, uint32_t h, ChronicleSiteKind kind)
{
  static const char *prefixes[] = {
      "Ash", "Reed", "Thorn", "Oak", "Iron", "Mist", "Gold", "Wolf", "River", "Stone",
      "Hill", "Marsh", "Pine", "Amber", "Crow"};
  static const char *suffixes[] = {
      "ford", "haven", "dale", "wick", "stead", "bury", "moor", "gate", "fall", "holm"};
  static const char *ruin_names[] = {"Broken Spire", "Ashen Court", "Silent Keep", "Old Watch"};
  static const char *lair_names[] = {"Deep Maw", "Webbed Hollow", "Scorched Den", "Bone Pit"};
  static const char *shrine_names[] = {"Waystone", "Soul Cairn", "Quiet Altar", "Dawn Shrine"};

  if (kind == CHRONICLE_SITE_RUIN)
  {
    snprintf(out, n, "%s", ruin_names[h % 4]);
    return;
  }
  if (kind == CHRONICLE_SITE_LAIR)
  {
    snprintf(out, n, "%s", lair_names[h % 4]);
    return;
  }
  if (kind == CHRONICLE_SITE_SHRINE)
  {
    snprintf(out, n, "%s", shrine_names[h % 4]);
    return;
  }
  snprintf(out, n, "%s%s", prefixes[h % 15], suffixes[(h >> 4) % 10]);
}

static void pick_civ_name(char *out, size_t n, uint32_t h, ChronicleEthic ethic)
{
  static const char *people[] = {
      "Riverfolk", "Hill Clans", "Marsh Wardens", "Ashborn", "Skyward Kin",
      "Stone League", "Green Circuit", "Iron Choir", "Dust Riders", "Tidebound"};
  (void)ethic;
  snprintf(out, n, "the %s", people[h % 10]);
}

static void pick_figure_name(char *out, size_t n, uint32_t h)
{
  static const char *given[] = {"Alden", "Brynn", "Cora", "Doran", "Elsa", "Finn", "Greta", "Hugo"};
  static const char *sur[] = {"Reed", "Ashford", "Thorn", "Vale", "Flint", "Brook", "Hawke", "Stone"};
  snprintf(out, n, "%s %s", given[h % 8], sur[(h >> 3) % 8]);
}

static void pick_deity_myth(ChronicleCiv *civ, uint32_t h)
{
  static const char *deities[] = {
      "Lumen", "Vesper", "Kael", "Mira", "Thornfather", "The Still Voice", "Ashmother", "Tide"};
  static const char *myths[] = {
      "Souls wander until a vessel remembers them.",
      "The first stone was a promise between sky and deep.",
      "War is a fever; peace is the cool of river water.",
      "Trade binds strangers closer than blood.",
      "Curiosity is the spark that wakes the immortal spark.",
      "Piety is listening when the wind names you."};
  strncpy(civ->deity, deities[h % 8], CHRONICLE_NAME_MAX - 1);
  strncpy(civ->myth, myths[(h >> 3) % 6], CHRONICLE_LINE_MAX - 1);
  static const char *symbols[] = {"spiral", "oak leaf", "open eye", "crossed picks", "wave", "star"};
  strncpy(civ->symbol, symbols[(h >> 6) % 6], CHRONICLE_NAME_MAX - 1);
}

static UniverseBiomeId biome_for_cell(int gx, int gy)
{
  // Lightweight proxy without a World: hash climate axes.
  uint32_t h = chron_hash((uint32_t)gx * 0x9E3779B1u ^ (uint32_t)gy * 0x85EBCA6Bu);
  float t = (float)((h >> 0) & 255) / 255.0f;
  float m = (float)((h >> 8) & 255) / 255.0f;
  float e = (float)((h >> 16) & 255) / 255.0f;
  float v = (float)((h >> 24) & 255) / 255.0f;
  UniverseClimate climate = {t, m, e, v * 0.35f};
  return universe_biome_classify(&climate).primary;
}

static void collect_sites(Chronicle *c, uint32_t *rng)
{
  const int R = CHRONICLE_SCAN_RADIUS;
  for (int gy = -R; gy <= R && c->site_count < CHRONICLE_MAX_SITES; gy++)
  {
    for (int gx = -R; gx <= R && c->site_count < CHRONICLE_MAX_SITES; gx++)
    {
      int scale = universe_settlement_scale(gx, gy);
      if (scale <= 0)
        continue;
      ChronicleSite *s = &c->sites[c->site_count++];
      memset(s, 0, sizeof(*s));
      s->id = chronicle_site_id_for_cell(gx, gy);
      s->gx = (int16_t)gx;
      s->gy = (int16_t)gy;
      s->scale = (uint8_t)scale;
      s->founded_year = (uint16_t)(1 + (chron_rng(rng) % 80));
      uint32_t roll = chron_rng(rng);
      s->kind = kind_from_scale(scale, roll, false);
      s->has_lair = (s->kind == CHRONICLE_SITE_LAIR) || ((roll % 90u) == 0 && scale >= 4);
      pick_site_name(s->name, sizeof(s->name), chron_rng(rng), s->kind);
    }
  }
}

static void create_civs(Chronicle *c, uint32_t *rng)
{
  // Seed a few civs from ethic tags.
  static const ChronicleEthic ethics[] = {
      CHRONICLE_ETHIC_PEACEFUL, CHRONICLE_ETHIC_WARLIKE, CHRONICLE_ETHIC_TRADER,
      CHRONICLE_ETHIC_PIOUS, CHRONICLE_ETHIC_CURIOUS, CHRONICLE_ETHIC_PEACEFUL};
  int n = 4 + (int)(chron_rng(rng) % 3);
  if (n > CHRONICLE_MAX_CIVS)
    n = CHRONICLE_MAX_CIVS;
  for (int i = 0; i < n; i++)
  {
    ChronicleCiv *civ = &c->civs[c->civ_count++];
    memset(civ, 0, sizeof(*civ));
    civ->id = chron_hash(0xC1B00000u + (uint32_t)i * 97u + *rng);
    if (civ->id == 0)
      civ->id = 1u + (uint32_t)i;
    civ->ethic = ethics[i % 6];
    civ->preferred_biome = (uint8_t)(chron_rng(rng) % UNIVERSE_BIOME_COUNT);
    civ->strength = (int16_t)(40 + (chron_rng(rng) % 40));
    pick_civ_name(civ->name, sizeof(civ->name), chron_rng(rng), civ->ethic);
    pick_deity_myth(civ, chron_rng(rng));
    chron_push_event(c, CHRONICLE_EV_CIV_FOUND, 1, civ->id, 0, 0, 0, 0, 0, civ->strength);
  }
}

static uint32_t best_civ_for_site(const Chronicle *c, const ChronicleSite *site, uint32_t *rng)
{
  if (c->civ_count == 0)
    return 0;
  UniverseBiomeId biome = biome_for_cell(site->gx, site->gy);
  uint32_t best = c->civs[0].id;
  int best_score = -9999;
  for (uint32_t i = 0; i < c->civ_count; i++)
  {
    const ChronicleCiv *civ = &c->civs[i];
    int score = civ->strength;
    if (civ->preferred_biome == (uint8_t)biome)
      score += 30;
    // Prefer hubs (roads) for traders / warlike.
    if (site->scale > 5)
      score += 10;
    score += (int)(chron_rng(rng) % 17u) - 8;
    if (score > best_score)
    {
      best_score = score;
      best = civ->id;
    }
  }
  return best;
}

static ChronicleCiv *civ_mut(Chronicle *c, uint32_t id)
{
  for (uint32_t i = 0; i < c->civ_count; i++)
    if (c->civs[i].id == id)
      return &c->civs[i];
  return NULL;
}

static ChronicleSite *site_mut(Chronicle *c, uint32_t id)
{
  for (uint32_t i = 0; i < c->site_count; i++)
    if (c->sites[i].id == id)
      return &c->sites[i];
  return NULL;
}

static void assign_sites_and_found(Chronicle *c, uint32_t *rng)
{
  for (uint32_t i = 0; i < c->site_count; i++)
  {
    ChronicleSite *s = &c->sites[i];
    s->civ_id = best_civ_for_site(c, s, rng);
    chron_push_event(c, CHRONICLE_EV_SITE_FOUND, s->founded_year, s->civ_id, 0, s->id, 0, 0, 0,
                     (int16_t)s->scale);

    if (c->figure_count < CHRONICLE_MAX_FIGURES)
    {
      ChronicleFigure *f = &c->figures[c->figure_count++];
      memset(f, 0, sizeof(*f));
      f->id = chron_hash(s->id ^ 0xF16u ^ *rng);
      if (!f->id)
        f->id = 1u + c->figure_count;
      f->civ_id = s->civ_id;
      f->home_site_id = s->id;
      f->birth_year = s->founded_year;
      pick_figure_name(f->name, sizeof(f->name), chron_rng(rng));
      chron_push_event(c, CHRONICLE_EV_FIGURE_RISE, s->founded_year, s->civ_id, 0, s->id, f->id, 0,
                       0, 0);
      chron_push_event(c, CHRONICLE_EV_FAMILY_FOUND, s->founded_year, s->civ_id, 0, s->id, f->id,
                       0, f->id, 0);
    }
  }
}

static void run_epochs(Chronicle *c, uint32_t *rng, uint16_t years)
{
  if (years < 50)
    years = 50;
  c->years_simulated = years;

  for (uint16_t y = 20; y <= years; y = (uint16_t)(y + 5 + (chron_rng(rng) % 7u)))
  {
    if (c->event_count + 8 >= CHRONICLE_MAX_EVENTS)
      break;
    if (c->site_count == 0 || c->civ_count == 0)
      break;

    uint32_t pick = chron_rng(rng) % 100u;
    ChronicleSite *site = &c->sites[chron_rng(rng) % c->site_count];
    ChronicleCiv *civ = civ_mut(c, site->civ_id);

    if (pick < 18 && c->civ_count >= 2)
    {
      // Raid
      uint32_t other = c->civs[chron_rng(rng) % c->civ_count].id;
      if (other == site->civ_id)
        other = c->civs[(chron_rng(rng) + 1) % c->civ_count].id;
      chron_push_event(c, CHRONICLE_EV_WAR_RAID, y, site->civ_id, other, site->id, 0, 0, 0, -5);
      if (civ)
        civ->strength = (int16_t)(civ->strength - 3);
      ChronicleCiv *oc = civ_mut(c, other);
      if (oc)
        oc->strength = (int16_t)(oc->strength + 2);
      if ((chron_rng(rng) % 5u) == 0 && site->scale >= 3)
      {
        site->ruin = true;
        site->kind = CHRONICLE_SITE_RUIN;
        chron_push_event(c, CHRONICLE_EV_SITE_ABANDON, y, site->civ_id, other, site->id, 0, 0, 0,
                         0);
      }
    }
    else if (pick < 28)
    {
      chron_push_event(c, CHRONICLE_EV_PEACE, y, site->civ_id,
                       c->civs[chron_rng(rng) % c->civ_count].id, site->id, 0, 0, 0, 2);
      if (civ)
        civ->strength = (int16_t)(civ->strength + 2);
    }
    else if (pick < 40)
    {
      chron_push_event(c, CHRONICLE_EV_MIGRATION, y, site->civ_id, 0, site->id, 0, 0, 0, 1);
    }
    else if (pick < 48)
    {
      // Trade along road/trade corridors between linked sites.
      ChronicleSite *peer = NULL;
      for (uint32_t si = 0; si < c->site_count; si++)
      {
        ChronicleSite *cand = &c->sites[si];
        if (cand->id == site->id || cand->ruin)
          continue;
        if (!universe_settlements_linked(site->gx, site->gy, cand->gx, cand->gy, NULL))
          continue;
        peer = cand;
        break;
      }
      if (peer)
      {
        chron_push_event(c, CHRONICLE_EV_TRADE, y, site->civ_id, peer->civ_id, site->id, 0, 0, 0,
                         (int16_t)peer->scale);
        if (civ)
          civ->strength = (int16_t)(civ->strength + 1);
        ChronicleCiv *pc = civ_mut(c, peer->civ_id);
        if (pc)
          pc->strength = (int16_t)(pc->strength + 1);
      }
      else
      {
        chron_push_event(c, CHRONICLE_EV_PEACE, y, site->civ_id, 0, site->id, 0, 0, 0, 1);
      }
    }
    else if (pick < 58)
    {
      chron_push_event(c, CHRONICLE_EV_PLAGUE, y, site->civ_id, 0, site->id, 0, 0, 0, -4);
      if (civ)
        civ->strength = (int16_t)(civ->strength - 4);
    }
    else if (pick < 62 || site->has_lair)
    {
      chron_push_event(c, CHRONICLE_EV_BEAST_ATTACK, y, site->civ_id, 0, site->id, 0, 0, 0, -2);
      if ((chron_rng(rng) % 4u) == 0)
      {
        site->has_lair = true;
        if (!site->ruin && site->kind != CHRONICLE_SITE_SHRINE)
          site->kind = CHRONICLE_SITE_LAIR;
      }
    }
    else if (pick < 75 && c->artifact_count < CHRONICLE_MAX_ARTIFACTS)
    {
      ChronicleArtifact *a = &c->artifacts[c->artifact_count++];
      memset(a, 0, sizeof(*a));
      a->id = chron_hash(0xA47u ^ *rng ^ (uint32_t)y);
      if (!a->id)
        a->id = 1u + c->artifact_count;
      a->site_id = site->id;
      a->civ_id = site->civ_id;
      a->created_year = y;
      // Attach a figure if any at site
      for (uint32_t fi = 0; fi < c->figure_count; fi++)
      {
        if (c->figures[fi].home_site_id == site->id && c->figures[fi].death_year == 0)
        {
          a->creator_figure_id = c->figures[fi].id;
          break;
        }
      }
      static const char *themes[] = {"ember", "tide", "oath", "memory", "fang", "glass"};
      static const char *anames[] = {"Relic", "Crown", "Blade", "Chalice", "Mask", "Tome"};
      uint32_t rh = chron_rng(rng);
      snprintf(a->theme, sizeof(a->theme), "%s", themes[rh % 6]);
      snprintf(a->name, sizeof(a->name), "%s of %s", anames[(rh >> 3) % 6], a->theme);
      chron_push_event(c, CHRONICLE_EV_ARTIFACT_CREATED, y, site->civ_id, 0, site->id,
                       a->creator_figure_id, a->id, 0, 0);
    }
    else if (pick < 88 && c->figure_count > 0)
    {
      ChronicleFigure *f = &c->figures[chron_rng(rng) % c->figure_count];
      if (f->death_year == 0 && (chron_rng(rng) % 3u) == 0)
      {
        f->death_year = y;
        chron_push_event(c, CHRONICLE_EV_FIGURE_DEATH, y, f->civ_id, 0, f->home_site_id, f->id, 0,
                         0, 0);
      }
      else if (c->figure_count < CHRONICLE_MAX_FIGURES)
      {
        ChronicleFigure *nf = &c->figures[c->figure_count++];
        memset(nf, 0, sizeof(*nf));
        nf->id = chron_hash(0xF17u ^ *rng ^ (uint32_t)y);
        if (!nf->id)
          nf->id = 1u + c->figure_count;
        nf->civ_id = site->civ_id;
        nf->home_site_id = site->id;
        nf->birth_year = y;
        pick_figure_name(nf->name, sizeof(nf->name), chron_rng(rng));
        chron_push_event(c, CHRONICLE_EV_FIGURE_RISE, y, site->civ_id, 0, site->id, nf->id, 0, 0,
                         0);
      }
    }
    else
    {
      chron_push_event(c, CHRONICLE_EV_PEACE, y, site->civ_id, 0, site->id, 0, 0, 0, 1);
    }

    // Clamp strengths
    for (uint32_t i = 0; i < c->civ_count; i++)
    {
      if (c->civs[i].strength < 5)
        c->civs[i].strength = 5;
      if (c->civs[i].strength > 120)
        c->civs[i].strength = 120;
    }
  }
  c->current_year = years;
}

bool chronicle_generate(Chronicle *c, const char *seed_hex, uint16_t years)
{
  if (!c)
    return false;
  chronicle_clear(c);
  if (seed_hex && seed_hex[0])
  {
    strncpy(c->seed, seed_hex, sizeof(c->seed) - 1);
    c->seed[sizeof(c->seed) - 1] = '\0';
  }
  uint32_t rng = chron_seed_u32(c->seed[0] ? c->seed : seed_hex);
  collect_sites(c, &rng);
  create_civs(c, &rng);
  assign_sites_and_found(c, &rng);
  if (years == 0)
    years = CHRONICLE_YEARS_DEFAULT;
  run_epochs(c, &rng, years);
  return c->site_count > 0 || c->civ_count > 0;
}

int chronicle_query_by_site(const Chronicle *c, uint32_t site_id, const ChronicleEvent **out,
                            int max_out)
{
  if (!c || !out || max_out <= 0 || !site_id)
    return 0;
  int n = 0;
  for (uint32_t i = 0; i < c->event_count && n < max_out; i++)
  {
    if (c->events[i].site_id == site_id)
      out[n++] = &c->events[i];
  }
  return n;
}

int chronicle_query_by_civ(const Chronicle *c, uint32_t civ_id, const ChronicleEvent **out,
                           int max_out)
{
  if (!c || !out || max_out <= 0 || !civ_id)
    return 0;
  int n = 0;
  for (uint32_t i = 0; i < c->event_count && n < max_out; i++)
  {
    if (c->events[i].civ_a == civ_id || c->events[i].civ_b == civ_id)
      out[n++] = &c->events[i];
  }
  return n;
}

int chronicle_query_by_figure(const Chronicle *c, uint32_t figure_id, const ChronicleEvent **out,
                              int max_out)
{
  if (!c || !out || max_out <= 0 || !figure_id)
    return 0;
  int n = 0;
  for (uint32_t i = 0; i < c->event_count && n < max_out; i++)
  {
    if (c->events[i].figure_id == figure_id)
      out[n++] = &c->events[i];
  }
  return n;
}

int chronicle_query_recent(const Chronicle *c, int max_out, const ChronicleEvent **out)
{
  if (!c || !out || max_out <= 0)
    return 0;
  // Newest first: walk events from the end of the append-only log.
  int n = 0;
  for (int i = (int)c->event_count - 1; i >= 0 && n < max_out; i--)
    out[n++] = &c->events[i];
  return n;
}

// Fill out with up to max_out site events, newest first.
int chronicle_query_by_site_recent(const Chronicle *c, uint32_t site_id, const ChronicleEvent **out,
                                   int max_out)
{
  if (!c || !out || max_out <= 0 || !site_id)
    return 0;
  int n = 0;
  for (int i = (int)c->event_count - 1; i >= 0 && n < max_out; i--)
  {
    if (c->events[i].site_id == site_id)
      out[n++] = &c->events[i];
  }
  return n;
}

const char *chronicle_format_line(const Chronicle *c, const ChronicleEvent *ev, char *buf,
                                  size_t buf_len)
{
  if (!buf || buf_len == 0)
    return "";
  buf[0] = '\0';
  if (!c || !ev)
    return buf;

  const ChronicleSite *site = chronicle_site(c, ev->site_id);
  const ChronicleCiv *civ = chronicle_civ(c, ev->civ_a);
  const ChronicleCiv *civ_b = chronicle_civ(c, ev->civ_b);
  const ChronicleFigure *fig = chronicle_figure(c, ev->figure_id);
  const ChronicleArtifact *art = chronicle_artifact(c, ev->artifact_id);
  const char *site_n = site ? site->name : "the wilds";
  const char *civ_n = civ ? civ->name : "a people";
  const char *fig_n = fig ? fig->name : "someone";

  switch (ev->type)
  {
  case CHRONICLE_EV_CIV_FOUND:
    snprintf(buf, buf_len, "Year %u: %s rose.", (unsigned)ev->year, civ_n);
    break;
  case CHRONICLE_EV_SITE_FOUND:
    snprintf(buf, buf_len, "Year %u: %s founded %s.", (unsigned)ev->year, civ_n, site_n);
    break;
  case CHRONICLE_EV_SITE_ABANDON:
    snprintf(buf, buf_len, "Year %u: %s was abandoned.", (unsigned)ev->year, site_n);
    break;
  case CHRONICLE_EV_WAR_RAID:
    snprintf(buf, buf_len, "Year %u: %s raided %s (%s).", (unsigned)ev->year, civ_n, site_n,
             civ_b ? civ_b->name : "foes");
    break;
  case CHRONICLE_EV_PEACE:
    snprintf(buf, buf_len, "Year %u: Peace held at %s.", (unsigned)ev->year, site_n);
    break;
  case CHRONICLE_EV_MIGRATION:
    snprintf(buf, buf_len, "Year %u: Folk migrated through %s.", (unsigned)ev->year, site_n);
    break;
  case CHRONICLE_EV_PLAGUE:
    snprintf(buf, buf_len, "Year %u: Plague touched %s.", (unsigned)ev->year, site_n);
    break;
  case CHRONICLE_EV_BEAST_ATTACK:
    snprintf(buf, buf_len, "Year %u: A beast struck %s.", (unsigned)ev->year, site_n);
    break;
  case CHRONICLE_EV_ARTIFACT_CREATED:
    snprintf(buf, buf_len, "Year %u: %s crafted %s at %s.", (unsigned)ev->year, fig_n,
             art ? art->name : "a relic", site_n);
    break;
  case CHRONICLE_EV_FIGURE_RISE:
    snprintf(buf, buf_len, "Year %u: %s rose to note in %s.", (unsigned)ev->year, fig_n, site_n);
    break;
  case CHRONICLE_EV_FIGURE_DEATH:
    snprintf(buf, buf_len, "Year %u: %s died.", (unsigned)ev->year, fig_n);
    break;
  case CHRONICLE_EV_FAMILY_FOUND:
    snprintf(buf, buf_len, "Year %u: The line of %s began at %s.", (unsigned)ev->year, fig_n,
             site_n);
    break;
  case CHRONICLE_EV_TRADE:
    snprintf(buf, buf_len, "Year %u: Caravans traded at %s with %s.", (unsigned)ev->year, site_n,
             civ_b ? civ_b->name : "distant markets");
    break;
  default:
    snprintf(buf, buf_len, "Year %u: %s.", (unsigned)ev->year, chronicle_event_type_name(ev->type));
    break;
  }
  return buf;
}

bool chronicle_site_label(const Chronicle *c, int gx, int gy, char *buf, size_t buf_len)
{
  if (!buf || buf_len == 0)
    return false;
  buf[0] = '\0';
  const ChronicleSite *s = chronicle_site_at(c, gx, gy);
  if (!s)
  {
    int scale = universe_settlement_scale(gx, gy);
    if (scale <= 0)
      return false;
    snprintf(buf, buf_len, "%s", settlement_scale_label(scale));
    return true;
  }
  const ChronicleCiv *civ = chronicle_civ(c, s->civ_id);
  if (civ)
    snprintf(buf, buf_len, "%s of %s", s->name, civ->name);
  else
    snprintf(buf, buf_len, "%s", s->name);
  return true;
}
