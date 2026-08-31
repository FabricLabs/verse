#ifndef GAME_LOG_H
#define GAME_LOG_H

#include <stdbool.h>
#include <stdint.h>

// On-screen scrolling game log (chronicle announcements + Global chat).

#define GAME_LOG_LINES 10
#define GAME_LOG_LINE_MAX 96
#define GAME_CHAT_INPUT_MAX 128

typedef enum {
  GAME_LOG_SYSTEM = 0,
  GAME_LOG_CHRONICLE,
  GAME_LOG_GLOBAL,
  GAME_LOG_LOCAL
} GameLogChannel;

typedef struct {
  char lines[GAME_LOG_LINES][GAME_LOG_LINE_MAX];
  GameLogChannel channels[GAME_LOG_LINES];
  uint8_t head;  // next write slot
  uint8_t count; // filled lines

  bool chat_open;
  char chat_input[GAME_CHAT_INPUT_MAX];
  int chat_len;
} GameLog;

void game_log_init(GameLog *log);
void game_log_clear(GameLog *log);

// Push a line (newest appears at the top of the on-screen stack).
void game_log_push(GameLog *log, GameLogChannel channel, const char *text);

// Chat: Enter opens; Enter again sends to Global; Esc cancels.
void game_log_chat_open(GameLog *log);
void game_log_chat_close(GameLog *log);
bool game_log_chat_is_open(const GameLog *log);
// Append UTF-8 text while chat is open. Returns false if not open / full.
bool game_log_chat_text(GameLog *log, const char *utf8);
void game_log_chat_backspace(GameLog *log);
// Send current input to Global and close. Empty input just closes.
bool game_log_chat_send(GameLog *log, const char *speaker_name);

// Read newest-first: index 0 is most recent. Returns NULL if out of range.
const char *game_log_line(const GameLog *log, int newest_index);
GameLogChannel game_log_line_channel(const GameLog *log, int newest_index);
int game_log_line_count(const GameLog *log);

bool game_log_channel_is_major_chronicle(int chronicle_event_type);

#endif // GAME_LOG_H
