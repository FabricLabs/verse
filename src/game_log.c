#include "game_log.h"
#include "chronicle.h"

#include <stdio.h>
#include <string.h>

void game_log_init(GameLog *log)
{
  if (!log)
    return;
  memset(log, 0, sizeof(*log));
}

void game_log_clear(GameLog *log)
{
  game_log_init(log);
}

void game_log_push(GameLog *log, GameLogChannel channel, const char *text)
{
  if (!log || !text || !text[0])
    return;
  snprintf(log->lines[log->head], GAME_LOG_LINE_MAX, "%s", text);
  log->channels[log->head] = channel;
  log->head = (uint8_t)((log->head + 1) % GAME_LOG_LINES);
  if (log->count < GAME_LOG_LINES)
    log->count++;
}

void game_log_chat_open(GameLog *log)
{
  if (!log)
    return;
  log->chat_open = true;
  log->chat_len = 0;
  log->chat_input[0] = '\0';
}

void game_log_chat_close(GameLog *log)
{
  if (!log)
    return;
  log->chat_open = false;
  log->chat_len = 0;
  log->chat_input[0] = '\0';
}

bool game_log_chat_is_open(const GameLog *log)
{
  return log && log->chat_open;
}

bool game_log_chat_text(GameLog *log, const char *utf8)
{
  if (!log || !log->chat_open || !utf8 || !utf8[0])
    return false;
  for (const unsigned char *p = (const unsigned char *)utf8; *p; p++)
  {
    if (log->chat_len >= GAME_CHAT_INPUT_MAX - 1)
      return true;
    // Keep printable ASCII for the low-res font.
    if (*p >= 32 && *p < 127)
      log->chat_input[log->chat_len++] = (char)*p;
  }
  log->chat_input[log->chat_len] = '\0';
  return true;
}

void game_log_chat_backspace(GameLog *log)
{
  if (!log || !log->chat_open || log->chat_len <= 0)
    return;
  log->chat_input[--log->chat_len] = '\0';
}

bool game_log_chat_send(GameLog *log, const char *speaker_name)
{
  if (!log || !log->chat_open)
    return false;
  if (log->chat_len > 0)
  {
    char line[GAME_LOG_LINE_MAX];
    const char *who = (speaker_name && speaker_name[0]) ? speaker_name : "You";
    snprintf(line, sizeof(line), "[Global] %s: %s", who, log->chat_input);
    game_log_push(log, GAME_LOG_GLOBAL, line);
  }
  game_log_chat_close(log);
  return true;
}

static int game_log_slot_for_newest(const GameLog *log, int newest_index)
{
  if (!log || newest_index < 0 || newest_index >= (int)log->count)
    return -1;
  // head points at next write; newest is head-1.
  int slot = ((int)log->head - 1 - newest_index) % GAME_LOG_LINES;
  if (slot < 0)
    slot += GAME_LOG_LINES;
  return slot;
}

const char *game_log_line(const GameLog *log, int newest_index)
{
  int slot = game_log_slot_for_newest(log, newest_index);
  return slot < 0 ? NULL : log->lines[slot];
}

GameLogChannel game_log_line_channel(const GameLog *log, int newest_index)
{
  int slot = game_log_slot_for_newest(log, newest_index);
  return slot < 0 ? GAME_LOG_SYSTEM : log->channels[slot];
}

int game_log_line_count(const GameLog *log)
{
  return log ? (int)log->count : 0;
}

bool game_log_channel_is_major_chronicle(int chronicle_event_type)
{
  switch ((ChronicleEventType)chronicle_event_type)
  {
  case CHRONICLE_EV_CIV_FOUND:
  case CHRONICLE_EV_SITE_ABANDON:
  case CHRONICLE_EV_WAR_RAID:
  case CHRONICLE_EV_PLAGUE:
  case CHRONICLE_EV_BEAST_ATTACK:
  case CHRONICLE_EV_ARTIFACT_CREATED:
  case CHRONICLE_EV_FIGURE_DEATH:
    return true;
  default:
    return false;
  }
}
