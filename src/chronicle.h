#ifndef CHRONICLE_H
#define CHRONICLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Deterministic world history: civilizations, sites, epoch events, artifacts, and light culture.
// Rebuild from universe seed — do not persist event bytes for streaming worlds.

#define CHRONICLE_MAX_CIVS 12
#define CHRONICLE_MAX_SITES 256
#define CHRONICLE_MAX_EVENTS 1024
#define CHRONICLE_MAX_ARTIFACTS 64
#define CHRONICLE_MAX_FIGURES 128
#define CHRONICLE_NAME_MAX 40
#define CHRONICLE_LINE_MAX 160
#define CHRONICLE_YEARS_DEFAULT 400
#define CHRONICLE_SCAN_RADIUS 48

typedef enum {
  CHRONICLE_EV_NONE = 0,
  CHRONICLE_EV_CIV_FOUND,
  CHRONICLE_EV_SITE_FOUND,
  CHRONICLE_EV_SITE_ABANDON,
  CHRONICLE_EV_WAR_RAID,
  CHRONICLE_EV_PEACE,
  CHRONICLE_EV_MIGRATION,
  CHRONICLE_EV_PLAGUE,
  CHRONICLE_EV_BEAST_ATTACK,
  CHRONICLE_EV_ARTIFACT_CREATED,
  CHRONICLE_EV_FIGURE_RISE,
  CHRONICLE_EV_FIGURE_DEATH,
  CHRONICLE_EV_FAMILY_FOUND,
  CHRONICLE_EV_TRADE,
  CHRONICLE_EV_COUNT
} ChronicleEventType;

typedef enum {
  CHRONICLE_SITE_HAMLET = 0,
  CHRONICLE_SITE_TOWN,
  CHRONICLE_SITE_FORT,
  CHRONICLE_SITE_RUIN,
  CHRONICLE_SITE_LAIR,
  CHRONICLE_SITE_SHRINE,
  CHRONICLE_SITE_KIND_COUNT
} ChronicleSiteKind;

typedef enum {
  CHRONICLE_ETHIC_PEACEFUL = 0,
  CHRONICLE_ETHIC_WARLIKE,
  CHRONICLE_ETHIC_TRADER,
  CHRONICLE_ETHIC_PIOUS,
  CHRONICLE_ETHIC_CURIOUS,
  CHRONICLE_ETHIC_COUNT
} ChronicleEthic;

typedef struct {
  uint32_t id;
  char name[CHRONICLE_NAME_MAX];
  ChronicleEthic ethic;
  uint8_t preferred_biome; // UniverseBiomeId; 255 = any
  int16_t strength;        // abstract polity power
  char deity[CHRONICLE_NAME_MAX];
  char myth[CHRONICLE_LINE_MAX];
  char symbol[CHRONICLE_NAME_MAX];
} ChronicleCiv;

typedef struct {
  uint32_t id;
  int16_t gx, gy; // wilderness cell
  uint32_t civ_id;
  uint16_t founded_year;
  uint8_t scale; // settlement scale 1..9
  ChronicleSiteKind kind;
  bool ruin;
  bool has_lair;
  char name[CHRONICLE_NAME_MAX];
} ChronicleSite;

typedef struct {
  uint32_t id;
  uint32_t civ_id;
  uint32_t home_site_id;
  uint16_t birth_year;
  uint16_t death_year; // 0 = alive at end of gen
  char name[CHRONICLE_NAME_MAX];
} ChronicleFigure;

typedef struct {
  uint32_t id;
  uint32_t creator_figure_id;
  uint32_t site_id;
  uint32_t civ_id;
  uint16_t created_year;
  char name[CHRONICLE_NAME_MAX];
  char theme[CHRONICLE_NAME_MAX];
} ChronicleArtifact;

typedef struct {
  ChronicleEventType type;
  uint16_t year;
  uint32_t civ_a;
  uint32_t civ_b;
  uint32_t site_id;
  uint32_t figure_id;
  uint32_t artifact_id;
  uint32_t family_id; // optional; 0 unused
  int16_t param;      // strength delta, etc.
} ChronicleEvent;

typedef struct Chronicle {
  char seed[65];
  uint16_t years_simulated;
  uint16_t current_year; // end year after generate
  uint32_t civ_count;
  uint32_t site_count;
  uint32_t event_count;
  uint32_t figure_count;
  uint32_t artifact_count;
  ChronicleCiv civs[CHRONICLE_MAX_CIVS];
  ChronicleSite sites[CHRONICLE_MAX_SITES];
  ChronicleEvent events[CHRONICLE_MAX_EVENTS];
  ChronicleFigure figures[CHRONICLE_MAX_FIGURES];
  ChronicleArtifact artifacts[CHRONICLE_MAX_ARTIFACTS];
} Chronicle;

void chronicle_init(Chronicle *c);
void chronicle_clear(Chronicle *c);

// Full worldgen history from seed (sites, civs, epochs, artifacts, culture).
bool chronicle_generate(Chronicle *c, const char *seed_hex, uint16_t years);

const ChronicleCiv *chronicle_civ(const Chronicle *c, uint32_t civ_id);
const ChronicleSite *chronicle_site(const Chronicle *c, uint32_t site_id);
const ChronicleSite *chronicle_site_at(const Chronicle *c, int gx, int gy);
const ChronicleArtifact *chronicle_artifact(const Chronicle *c, uint32_t artifact_id);
const ChronicleFigure *chronicle_figure(const Chronicle *c, uint32_t figure_id);

// Stable site id for a wilderness cell (0 if no settlement).
uint32_t chronicle_site_id_for_cell(int gx, int gy);

int chronicle_query_by_site(const Chronicle *c, uint32_t site_id, const ChronicleEvent **out,
                            int max_out);
int chronicle_query_by_civ(const Chronicle *c, uint32_t civ_id, const ChronicleEvent **out,
                           int max_out);
int chronicle_query_by_figure(const Chronicle *c, uint32_t figure_id, const ChronicleEvent **out,
                              int max_out);
int chronicle_query_recent(const Chronicle *c, int max_out, const ChronicleEvent **out);
// Site events newest-first (legends / game-log).
int chronicle_query_by_site_recent(const Chronicle *c, uint32_t site_id, const ChronicleEvent **out,
                                   int max_out);

// Human-readable one-liner into buf (NUL-terminated). Returns buf.
const char *chronicle_format_line(const Chronicle *c, const ChronicleEvent *ev, char *buf,
                                  size_t buf_len);

const char *chronicle_event_type_name(ChronicleEventType t);
const char *chronicle_site_kind_name(ChronicleSiteKind k);

// Display label for waypoints: "Ashford of the Riverfolk"
bool chronicle_site_label(const Chronicle *c, int gx, int gy, char *buf, size_t buf_len);

#endif // CHRONICLE_H
