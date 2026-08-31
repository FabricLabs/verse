#include "console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL2/SDL_ttf.h>

#include "game_state.h"
#include "isometric_renderer.h"
#include "particle_effects.h"
#include "skill.h"
#include "actor.h"

// Global toggles so the renderer can read them without threading Console* through every call.
static bool g_console_wireframe = false;
static bool g_console_xray = false;
static bool g_console_fog = true; // map fog of war only; play views never hide unexplored voxels

struct Console
{
  bool open;
  char input[CONSOLE_INPUT_MAX];
  int input_len;
  char history[CONSOLE_HISTORY_LINES][CONSOLE_HISTORY_LINE_MAX];
  int history_count; // number of filled lines (capped)
  int history_head;  // next write index (ring)
};

extern GameState *g_game_state;
extern IsometricRenderer *g_isometric_renderer;

bool console_wireframe_enabled(void) { return g_console_wireframe; }
bool console_xray_enabled(void) { return g_console_xray; }
bool console_fog_enabled(void) { return g_console_fog; }

Console *console_create(void)
{
  Console *c = (Console *)calloc(1, sizeof(Console));
  return c;
}

void console_destroy(Console *console)
{
  free(console);
}

bool console_is_open(const Console *console)
{
  return console && console->open;
}

static void console_push_line(Console *console, const char *line)
{
  if (!console || !line)
    return;
  snprintf(console->history[console->history_head], CONSOLE_HISTORY_LINE_MAX, "%s", line);
  console->history_head = (console->history_head + 1) % CONSOLE_HISTORY_LINES;
  if (console->history_count < CONSOLE_HISTORY_LINES)
    console->history_count++;
}

void console_toggle(Console *console)
{
  if (!console)
    return;
  console->open = !console->open;
  if (console->open)
  {
    console->input_len = 0;
    console->input[0] = '\0';
    SDL_StartTextInput();
    if (console->history_count == 0)
      console_push_line(console, "Console ready. Type 'help' for commands.");
  }
  else
  {
    SDL_StopTextInput();
  }
}

void console_close(Console *console)
{
  if (!console || !console->open)
    return;
  console->open = false;
  console->input_len = 0;
  console->input[0] = '\0';
  SDL_StopTextInput();
}

static void console_cmd_help(Console *console)
{
  console_push_line(console, "Commands:");
  console_push_line(console, "  help       - list commands");
  console_push_line(console, "  fly        - toggle flying (disable gravity)");
  console_push_line(console, "  admin      - open world editor (gravity and world settings)");
  console_push_line(console, "  wireframe  - toggle voxel wireframe mode");
  console_push_line(console, "  xray       - toggle semi-transparent terrain");
  console_push_line(console, "  fog        - toggle fog of war on the world map (M)");
  console_push_line(console, "  rain       - toggle light rain weather effect");
  console_push_line(console, "  heavyrain  - toggle heavy rain (dense drops + ripples)");
  console_push_line(console, "  meteorstorm- toggle meteor/lava storm (universe-wide)");
  console_push_line(console, "  lavastorm  - alias for meteorstorm");
  console_push_line(console, "  thunderstorm - toggle thunderstorm (lightning + rain)");
  console_push_line(console, "  storm     - alias for thunderstorm");
  console_push_line(console, "  teleport   - teleport home | town <size>");
  console_push_line(console, "  godmode    - max all skill ranks; XP to highest III gate");
  console_push_line(console, "  clear      - clear console history");
  console_push_line(console, "  quit/exit  - close the console");
}

static void console_cmd_godmode(Console *console)
{
  if (!g_game_state)
  {
    console_push_line(console, "godmode: no game state");
    return;
  }
  if (!g_game_state->player)
  {
    console_push_line(console, "godmode: no player spirit");
    return;
  }

  uint8_t max_lv = 1;
  for (SkillId id = (SkillId)1; id < SKILL_COUNT; id++)
  {
    if (!skill_is_unlockable(id))
      continue;
    const uint8_t need = skill_min_level(id);
    if (need > max_lv)
      max_lv = need;
  }

  if (!skill_godmode(g_game_state))
  {
    console_push_line(console, "godmode: failed");
    return;
  }

  char msg[CONSOLE_HISTORY_LINE_MAX];
  snprintf(msg, sizeof(msg), "godmode: Lv %u (gate %u), all skills unlocked",
           g_game_state->player->level, (unsigned)max_lv);
  console_push_line(console, msg);
}

static void console_cmd_fly(Console *console)
{
  if (!g_game_state)
  {
    console_push_line(console, "fly: no game state");
    return;
  }
  g_game_state->player_flying = !g_game_state->player_flying;
  if (g_game_state->player)
    g_game_state->player->is_flying = g_game_state->player_flying;
  if (g_game_state->player_flying)
  {
    g_game_state->controls.velocity_z = 0.0f;
    console_push_line(console, "fly: ON (gravity disabled)");
  }
  else
  {
    console_push_line(console, "fly: OFF (gravity enabled)");
  }
}

static void console_cmd_admin(Console *console)
{
  if (!g_game_state)
  {
    console_push_line(console, "admin: no game state");
    return;
  }
  World *world = g_game_state->current_world ? g_game_state->current_world
                                             : g_game_state->main_menu_world;
  if (!world)
  {
    console_push_line(console, "admin: no world loaded");
    return;
  }
  g_game_state->show_world_editor_modal = true;
  console_push_line(console, "admin: world editor opened");
  console_close(console);
}

static void console_cmd_wireframe(Console *console)
{
  g_console_wireframe = !g_console_wireframe;
  if (g_isometric_renderer)
    g_isometric_renderer->wireframe_mode = g_console_wireframe;
  console_push_line(console, g_console_wireframe ? "wireframe: ON" : "wireframe: OFF");
}

static void console_cmd_xray(Console *console)
{
  g_console_xray = !g_console_xray;
  if (g_isometric_renderer)
    g_isometric_renderer->xray_mode = g_console_xray;
  console_push_line(console, g_console_xray ? "xray: ON (terrain translucent)"
                                            : "xray: OFF");
}

static void console_cmd_fog(Console *console)
{
  g_console_fog = !g_console_fog;
  console_push_line(console, g_console_fog ? "fog: ON (map fog of war)"
                                           : "fog: OFF (map shows full terrain)");
}

static void console_cmd_rain(Console *console)
{
  if (!g_game_state)
  {
    console_push_line(console, "rain: no game state");
    return;
  }
  World *world = g_game_state->current_world ? g_game_state->current_world
                                             : g_game_state->main_menu_world;
  if (!world)
  {
    console_push_line(console, "rain: no world");
    return;
  }
  bool enabled = !particle_effects_rain_enabled(world);
  particle_effects_set_rain_enabled(world, enabled);
  console_push_line(console, enabled ? "rain: ON" : "rain: OFF");
}

static void console_cmd_heavy_rain(Console *console)
{
  if (!g_game_state)
  {
    console_push_line(console, "heavyrain: no game state");
    return;
  }
  World *world = g_game_state->current_world ? g_game_state->current_world
                                             : g_game_state->main_menu_world;
  if (!world)
  {
    console_push_line(console, "heavyrain: no world");
    return;
  }
  bool enabled = !particle_effects_heavy_rain_enabled(world);
  particle_effects_set_heavy_rain_enabled(world, enabled);
  console_push_line(console, enabled ? "heavyrain: ON" : "heavyrain: OFF");
}

// Apply or clear the meteor/lava storm on every resident universe cell, and remember the choice
// so newly streamed worlds inherit it. Rain is suppressed while the storm is active.
static void console_cmd_meteor_storm(Console *console)
{
  if (!g_game_state)
  {
    console_push_line(console, "meteorstorm: no game state");
    return;
  }

  const bool enabled = !particle_effects_meteor_storm_universe_enabled();
  particle_effects_set_meteor_storm_universe_enabled(enabled);

  int touched = 0;
  Universe *u = &g_game_state->universe;
  if (u->entries)
  {
    for (size_t i = 0; i < u->capacity; i++)
    {
      if (!u->entries[i].used || !u->entries[i].w)
        continue;
      particle_effects_set_meteor_storm_enabled(u->entries[i].w, enabled);
      touched++;
    }
  }

  World *focus = g_game_state->current_world ? g_game_state->current_world
                                             : g_game_state->main_menu_world;
  if (focus)
  {
    particle_effects_set_meteor_storm_enabled(focus, enabled);
    if (touched == 0)
      touched = 1;
  }

  if (enabled)
    universe_append_event(u, "METEOR_STORM", "{\"state\":\"on\"}");
  else
    universe_append_event(u, "METEOR_STORM", "{\"state\":\"off\"}");

  char msg[CONSOLE_HISTORY_LINE_MAX];
  snprintf(msg, sizeof(msg),
           enabled ? "meteorstorm: ON (lava meteors on %d world%s)"
                   : "meteorstorm: OFF (%d world%s cleared)",
           touched, touched == 1 ? "" : "s");
  console_push_line(console, msg);
}


// Apply or clear a thunderstorm on every resident universe cell (lightning + heavy rain).
static void console_cmd_thunderstorm(Console *console)
{
  if (!g_game_state)
  {
    console_push_line(console, "thunderstorm: no game state");
    return;
  }

  const bool enabled = !particle_effects_thunderstorm_universe_enabled();
  particle_effects_set_thunderstorm_universe_enabled(enabled);

  int touched = 0;
  Universe *u = &g_game_state->universe;
  if (u->entries)
  {
    for (size_t i = 0; i < u->capacity; i++)
    {
      if (!u->entries[i].used || !u->entries[i].w)
        continue;
      particle_effects_set_thunderstorm_enabled(u->entries[i].w, enabled);
      touched++;
    }
  }

  World *focus = g_game_state->current_world ? g_game_state->current_world
                                             : g_game_state->main_menu_world;
  if (focus)
  {
    particle_effects_set_thunderstorm_enabled(focus, enabled);
    if (touched == 0)
      touched = 1;
  }

  if (enabled)
    universe_append_event(u, "THUNDERSTORM", "{\"state\":\"on\"}");
  else
    universe_append_event(u, "THUNDERSTORM", "{\"state\":\"off\"}");

  char msg[CONSOLE_HISTORY_LINE_MAX];
  snprintf(msg, sizeof(msg),
           enabled ? "thunderstorm: ON (lightning on %d world%s)"
                   : "thunderstorm: OFF (%d world%s cleared)",
           touched, touched == 1 ? "" : "s");
  console_push_line(console, msg);
}

// `teleport home` — return to the home island.
// `teleport town <size>` — nearest wilderness settlement of exact scale 1..9.
static void console_cmd_teleport(Console *console, const char *args)
{
  if (!console)
    return;
  if (!g_game_state)
  {
    console_push_line(console, "teleport: no game state");
    return;
  }
  if (!g_game_state->game_started)
  {
    console_push_line(console, "teleport: game not started");
    return;
  }

  while (args && *args && isspace((unsigned char)*args))
    args++;
  if (!args || !*args)
  {
    console_push_line(console, "teleport: usage: teleport home | teleport town <size>");
    return;
  }

  char dest[32];
  size_t i = 0;
  while (args[i] && !isspace((unsigned char)args[i]) && i + 1 < sizeof(dest))
  {
    dest[i] = (char)tolower((unsigned char)args[i]);
    i++;
  }
  dest[i] = '\0';
  const char *rest = args + i;
  while (*rest && isspace((unsigned char)*rest))
    rest++;

  if (strcmp(dest, "home") == 0)
  {
    if (game_state_teleport_home(g_game_state))
      console_push_line(console, "teleport: home");
    else
      console_push_line(console, "teleport: failed to reach home");
    return;
  }

  if (strcmp(dest, "town") == 0)
  {
    if (!*rest)
    {
      console_push_line(console, "teleport: usage: teleport town <size> (1-9)");
      return;
    }
    char *end = NULL;
    long size = strtol(rest, &end, 10);
    if (end == rest || size < 1 || size > 9)
    {
      console_push_line(console, "teleport: size must be an integer 1-9");
      return;
    }
    if (game_state_teleport_to_settlement(g_game_state, (int)size))
    {
      char msg[CONSOLE_HISTORY_LINE_MAX];
      snprintf(msg, sizeof(msg), "teleport: town size %ld", size);
      console_push_line(console, msg);
    }
    else
    {
      char msg[CONSOLE_HISTORY_LINE_MAX];
      snprintf(msg, sizeof(msg), "teleport: no size-%ld settlement nearby", size);
      console_push_line(console, msg);
    }
    return;
  }

  console_push_line(console, "teleport: usage: teleport home | teleport town <size>");
}

static void console_execute(Console *console, const char *raw)
{
  if (!console || !raw)
    return;

  // Trim leading/trailing whitespace
  while (*raw && isspace((unsigned char)*raw))
    raw++;
  char buf[CONSOLE_INPUT_MAX];
  snprintf(buf, sizeof(buf), "%s", raw);
  size_t n = strlen(buf);
  while (n > 0 && isspace((unsigned char)buf[n - 1]))
    buf[--n] = '\0';
  if (n == 0)
    return;

  char echoed[CONSOLE_HISTORY_LINE_MAX];
  snprintf(echoed, sizeof(echoed), "> %s", buf);
  console_push_line(console, echoed);

  // Lowercase command token
  char cmd[64];
  size_t i = 0;
  while (buf[i] && !isspace((unsigned char)buf[i]) && i + 1 < sizeof(cmd))
  {
    cmd[i] = (char)tolower((unsigned char)buf[i]);
    i++;
  }
  cmd[i] = '\0';

  // Remainder after the command token (leading whitespace already skipped below).
  const char *args = buf + i;
  while (*args && isspace((unsigned char)*args))
    args++;

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
    console_cmd_help(console);
  else if (strcmp(cmd, "fly") == 0)
    console_cmd_fly(console);
  else if (strcmp(cmd, "admin") == 0)
    console_cmd_admin(console);
  else if (strcmp(cmd, "wireframe") == 0 || strcmp(cmd, "wire") == 0)
    console_cmd_wireframe(console);
  else if (strcmp(cmd, "xray") == 0)
    console_cmd_xray(console);
  else if (strcmp(cmd, "fog") == 0)
    console_cmd_fog(console);
  else if (strcmp(cmd, "rain") == 0)
    console_cmd_rain(console);
  else if (strcmp(cmd, "heavyrain") == 0 || strcmp(cmd, "heavy_rain") == 0)
    console_cmd_heavy_rain(console);
  else if (strcmp(cmd, "meteorstorm") == 0 || strcmp(cmd, "meteor_storm") == 0 ||
           strcmp(cmd, "lavastorm") == 0 || strcmp(cmd, "lava_storm") == 0 ||
           strcmp(cmd, "meteor") == 0 || strcmp(cmd, "lava") == 0)
    console_cmd_meteor_storm(console);
  else if (strcmp(cmd, "thunderstorm") == 0 || strcmp(cmd, "thunder_storm") == 0 ||
           strcmp(cmd, "storm") == 0 || strcmp(cmd, "lightning") == 0)
    console_cmd_thunderstorm(console);
  else if (strcmp(cmd, "teleport") == 0 || strcmp(cmd, "tp") == 0)
    console_cmd_teleport(console, args);
  else if (strcmp(cmd, "godmode") == 0 || strcmp(cmd, "god") == 0)
    console_cmd_godmode(console);
  else if (strcmp(cmd, "clear") == 0)
  {
    console->history_count = 0;
    console->history_head = 0;
  }
  else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0 || strcmp(cmd, "close") == 0)
    console_close(console);
  else
  {
    char msg[CONSOLE_HISTORY_LINE_MAX];
    snprintf(msg, sizeof(msg), "unknown command: %s (try 'help')", cmd);
    console_push_line(console, msg);
  }
}

bool console_handle_key(Console *console, SDL_Keycode key)
{
  if (!console)
    return false;

  // Tilde / backtick toggles regardless of open state (except during name entry — caller gates that).
  if (key == SDLK_BACKQUOTE)
  {
    console_toggle(console);
    return true;
  }

  if (!console->open)
    return false;

  if (key == SDLK_ESCAPE)
  {
    console_close(console);
    return true;
  }
  if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
  {
    console_execute(console, console->input);
    console->input_len = 0;
    console->input[0] = '\0';
    return true;
  }
  if (key == SDLK_BACKSPACE)
  {
    if (console->input_len > 0)
    {
      console->input_len--;
      console->input[console->input_len] = '\0';
    }
    return true;
  }

  // Swallow all other keys while open so WASD/etc. do not move the player.
  return true;
}

void console_handle_text(Console *console, const char *text)
{
  if (!console || !console->open || !text)
    return;

  for (const char *p = text; *p; p++)
  {
    unsigned char ch = (unsigned char)*p;
    // Skip the backtick itself if it arrives as text input when toggling.
    if (ch == '`' || ch == '~')
      continue;
    if (ch < 32 || ch > 126)
      continue;
    if (console->input_len + 1 >= CONSOLE_INPUT_MAX)
      break;
    console->input[console->input_len++] = (char)ch;
    console->input[console->input_len] = '\0';
  }
}

static void console_draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text,
                              int x, int y, SDL_Color color)
{
  if (!renderer || !font || !text || !text[0])
    return;
  SDL_Surface *surf = TTF_RenderText_Blended(font, text, color);
  if (!surf)
    return;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
  if (tex)
  {
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
  }
  SDL_FreeSurface(surf);
}

void console_render(Console *console, SDL_Renderer *renderer, TTF_Font *font,
                    int screen_w, int screen_h)
{
  if (!console || !console->open || !renderer)
    return;

  const int line_h = 11;
  const int pad = 6;
  const int visible_lines = 6;
  const int panel_h = pad * 2 + line_h * (visible_lines + 1) + 4;
  const int panel_y = screen_h - panel_h;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 8, 12, 20, 210);
  SDL_Rect bg = {0, panel_y, screen_w, panel_h};
  SDL_RenderFillRect(renderer, &bg);
  SDL_SetRenderDrawColor(renderer, 60, 90, 140, 255);
  SDL_RenderDrawLine(renderer, 0, panel_y, screen_w, panel_y);

  SDL_Color hist_color = {180, 200, 220, 255};
  SDL_Color input_color = {240, 245, 255, 255};
  SDL_Color prompt_color = {120, 200, 255, 255};

  // Oldest → newest within the visible window
  int start = console->history_count < visible_lines
                  ? 0
                  : console->history_count - visible_lines;
  for (int i = 0; i < console->history_count && i < visible_lines; i++)
  {
    int logical = start + i;
    int idx = (console->history_head - console->history_count + logical + CONSOLE_HISTORY_LINES * 2) %
              CONSOLE_HISTORY_LINES;
    int y = panel_y + pad + i * line_h;
    if (font)
      console_draw_text(renderer, font, console->history[idx], pad, y, hist_color);
  }

  int input_y = panel_y + pad + visible_lines * line_h;
  char prompt_line[CONSOLE_INPUT_MAX + 4];
  snprintf(prompt_line, sizeof(prompt_line), "> %s", console->input);
  if (font)
  {
    console_draw_text(renderer, font, prompt_line, pad, input_y, input_color);
    // Blinking caret approximation: always show a block after the text
    int tw = 0, th = 0;
    TTF_SizeText(font, prompt_line, &tw, &th);
    (void)th;
    SDL_SetRenderDrawColor(renderer, prompt_color.r, prompt_color.g, prompt_color.b, 220);
    SDL_Rect caret = {pad + tw + 1, input_y + 1, 7, line_h - 3};
    if (((SDL_GetTicks() / 400) % 2) == 0)
      SDL_RenderFillRect(renderer, &caret);
  }
  else
  {
    // Fallback if font is missing — still show that the console is open
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_Rect bar = {pad, input_y, screen_w - pad * 2, 4};
    SDL_RenderFillRect(renderer, &bar);
  }
}
