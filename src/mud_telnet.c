#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "world.h"
#include "engine.h"
#include "constants.h"
#include "mud_telnet.h"

#define MUD_MAX_CLIENTS 64
#define MUD_RECV_BUF 1024

// Basic telnet negotiation helpers
#define IAC 255
#define DONT 254
#define DO 253
#define WONT 252
#define WILL 251

#define TELOPT_ECHO 1
#define TELOPT_SGA 3

typedef struct {
  int sock;
  char inbuf[MUD_RECV_BUF];
  size_t inlen;
  char outbuf[2048];
  size_t outlen;
  char player_id[64];
  char world_id[64];
  int x, y, z;
  int connected;
} MudClient;

static volatile sig_atomic_t mud_running = 1;

static void mud_sig_handler(int sig) {
  (void)sig;
  mud_running = 0;
}

static int set_nonblocking_fd(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void telnet_send_negotiation(int sock) {
  // Prefer client-side local echo; we won't echo per-character from the server.
  // Request SGA (suppress go ahead) on both sides to reduce chatter.
  unsigned char nego[] = {
    IAC, WONT, TELOPT_ECHO,
    IAC, WILL, TELOPT_SGA,
    IAC, DO,   TELOPT_SGA
  };
  send(sock, nego, sizeof(nego), 0);
}

static void client_send_text(MudClient *c, const char *text) {
  if (!c) return;
  send(c->sock, text, (int)strlen(text), 0);
}

static void describe_location(MudClient *c, Engine *engine) {
  if (!c || !engine) return;
  World *w = engine_get_world(engine, c->world_id);
  if (!w) {
    client_send_text(c, "You are nowhere.\r\n");
    return;
  }
  // Report voxel under feet
  int vx = c->x, vy = c->y, vz = c->z;
  if (!world_is_position_valid(w, vx, vy, vz)) {
    client_send_text(c, "You are outside the world bounds.\r\n");
    return;
  }
  Voxel *v = world_get_voxel(w, vx, vy, vz);
  const char *vname = v ? world_voxel_type_name(v->type) : "unknown";
  char line[256];
  snprintf(line, sizeof(line), "You stand at (%d,%d,%d) on %s.\r\n", vx, vy, vz, vname);
  client_send_text(c, line);
}

static void move_player(MudClient *c, Engine *engine, int dx, int dz) {
  World *w = engine_get_world(engine, c->world_id);
  if (!w) return;
  int nx = c->x + dx;
  int nz = c->z + dz;
  int ny = c->y;
  if (world_is_position_valid(w, nx, ny, nz)) {
    Voxel *vv = world_get_voxel(w, nx, ny, nz);
    if (vv && (vv->type == VOXEL_GRASS || vv->type == VOXEL_SOIL)) {
      c->x = nx; c->z = nz;
      describe_location(c, engine);
      return;
    }
  }
  client_send_text(c, "You bump into something.\r\n");
}

static void handle_command(MudClient *c, Engine *engine, const char *line) {
  // Trim
  while (*line && isspace((unsigned char)*line)) line++;
  size_t n = strlen(line);
  while (n && (line[n-1] == '\r' || line[n-1] == '\n' || isspace((unsigned char)line[n-1]))) n--;
  char cmd[256];
  if (n >= sizeof(cmd)) n = sizeof(cmd) - 1;
  memcpy(cmd, line, n); cmd[n] = '\0';

  if (cmd[0] == '\0') return;

  if (!strcasecmp(cmd, "help")) {
    client_send_text(c,
      "Commands: help, look, say <msg>, n/s/e/w, where, quit\r\n");
    return;
  }
  if (!strcasecmp(cmd, "look")) {
    describe_location(c, engine);
    return;
  }
  if (!strncasecmp(cmd, "say ", 4)) {
    // Local echo only for now
    client_send_text(c, "You say: ");
    client_send_text(c, cmd + 4);
    client_send_text(c, "\r\n");
    return;
  }
  if (!strcasecmp(cmd, "n")) { move_player(c, engine, 0, -1); return; }
  if (!strcasecmp(cmd, "s")) { move_player(c, engine, 0,  1); return; }
  if (!strcasecmp(cmd, "w")) { move_player(c, engine, -1, 0); return; }
  if (!strcasecmp(cmd, "e")) { move_player(c, engine,  1, 0); return; }
  if (!strcasecmp(cmd, "where")) { describe_location(c, engine); return; }
  if (!strcasecmp(cmd, "quit")) { shutdown(c->sock, SHUT_RDWR); return; }

  client_send_text(c, "Unknown command. Type 'help'.\r\n");
}

// Very simple telnet IAC stripper: returns number of bytes of real data copied to out
static size_t telnet_strip_iac(const unsigned char *in, size_t inlen, unsigned char *out, size_t outcap) {
  size_t oi = 0;
  for (size_t i = 0; i < inlen && oi < outcap; ) {
    if (in[i] == IAC) {
      if (i + 1 < inlen) {
        unsigned char cmd = in[i+1];
        if (cmd == DO || cmd == DONT || cmd == WILL || cmd == WONT) {
          i += 3; // IAC CMD OPT
          continue;
        } else if (cmd == IAC) {
          // Escaped 255
          out[oi++] = IAC;
          i += 2;
          continue;
        } else {
          i += 2; // skip IAC <cmd>
          continue;
        }
      } else {
        // trailing IAC, drop
        i++;
        continue;
      }
    }
    out[oi++] = in[i++];
  }
  return oi;
}

int run_mud_telnet_server(const char *seed, int world_size, int port) {
  int listen_fd = -1;
  struct sockaddr_in addr;
  MudClient clients[MUD_MAX_CLIENTS];
  memset(clients, 0, sizeof(clients));
  for (int i = 0; i < MUD_MAX_CLIENTS; i++) clients[i].sock = -1;

  signal(SIGINT, mud_sig_handler);
  signal(SIGTERM, mud_sig_handler);

  // Prepare engine and world
  Engine *engine = engine_create();
  if (!engine) {
    fprintf(stderr, "MUD: failed to create engine\n");
    return 1;
  }

  World *world = world_create((uint32_t)world_size, (uint32_t)world_size, (uint32_t)world_size);
  if (!world) {
    fprintf(stderr, "MUD: failed to create world\n");
    engine_destroy(engine);
    return 1;
  }
  world_generate(world, seed);
  if (!engine_add_world_entry(engine, seed, world)) {
    fprintf(stderr, "MUD: failed to add world entry\n");
    world_destroy(world);
    engine_destroy(engine);
    return 1;
  }

  strncpy(engine->center_world_id, seed, sizeof(engine->center_world_id)-1);
  engine->center_world_id[sizeof(engine->center_world_id)-1] = '\0';

  // Create listener
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    engine_destroy(engine);
    return 1;
  }
  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((uint16_t)port);
  if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(listen_fd);
    engine_destroy(engine);
    return 1;
  }
  if (listen(listen_fd, 16) < 0) {
    perror("listen");
    close(listen_fd);
    engine_destroy(engine);
    return 1;
  }
  set_nonblocking_fd(listen_fd);
  printf("MUD telnet server listening on port %d (seed=%s, size=%d)\n", port, seed, world_size);

  while (mud_running) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listen_fd, &rfds);
    int maxfd = listen_fd;
    for (int i = 0; i < MUD_MAX_CLIENTS; i++) {
      if (clients[i].sock >= 0) {
        FD_SET(clients[i].sock, &rfds);
        if (clients[i].sock > maxfd) maxfd = clients[i].sock;
      }
    }
    struct timeval tv = {0, 100000};
    int n = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    if (n < 0) {
      if (errno == EINTR) continue;
      perror("select");
      break;
    }

    // Accept new
    if (FD_ISSET(listen_fd, &rfds)) {
      struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
      int cs = accept(listen_fd, (struct sockaddr*)&caddr, &clen);
      if (cs >= 0) {
        set_nonblocking_fd(cs);
        int slot = -1; for (int i = 0; i < MUD_MAX_CLIENTS; i++) if (clients[i].sock < 0) { slot = i; break; }
        if (slot >= 0) {
          MudClient *c = &clients[slot]; memset(c, 0, sizeof(*c)); c->sock = cs; c->inlen = 0; c->connected = 1;
          strncpy(c->world_id, seed, sizeof(c->world_id)-1);
          // Initial spawn: center on highest grass
          World *w = engine_get_world(engine, seed);
          int px = world_size/2, pz = world_size/2, py = 0;
          if (w) {
            for (int y = (int)w->height - 1; y >= 0; --y) {
              Voxel *vv = world_get_voxel(w, px, y, pz);
              if (vv && (vv->type == VOXEL_GRASS || vv->type == VOXEL_SOIL)) { py = y; break; }
            }
          }
          c->x = px; c->y = py; c->z = pz;
          telnet_send_negotiation(cs);
          client_send_text(c, "Welcome to VERSE MUD. Type 'help'.\r\n");
          describe_location(c, engine);
          client_send_text(c, "> ");
        } else {
          const char *full = "Server is full. Try again later.\r\n";
          send(cs, full, (int)strlen(full), 0);
          close(cs);
        }
      }
    }

    // Read existing
    for (int i = 0; i < MUD_MAX_CLIENTS; i++) {
      MudClient *c = &clients[i];
      if (c->sock < 0) continue;
      if (!FD_ISSET(c->sock, &rfds)) continue;
      unsigned char buf[MUD_RECV_BUF];
      int r = (int)recv(c->sock, buf, sizeof(buf), 0);
      if (r <= 0) {
        // Some telnet clients do not send data until IAC negotiation completes.
        // Treat EAGAIN/EWOULDBLOCK as no data, keep connection.
        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
        close(c->sock); c->sock = -1; c->connected = 0; continue;
      }
      unsigned char clean[MUD_RECV_BUF];
      size_t clen = telnet_strip_iac(buf, (size_t)r, clean, sizeof(clean));
      for (size_t k = 0; k < clen; k++) {
        char ch = (char)clean[k];
        if (ch == '\n') {
          c->inbuf[c->inlen] = '\0';
          handle_command(c, engine, c->inbuf);
          c->inlen = 0;
          // Send CRLF + prompt to be friendly to all telnet clients
          client_send_text(c, "\r\n> ");
        } else if (ch == '\r') {
          // Map CR to LF for clients that only send CR
          c->inbuf[c->inlen] = '\0';
          handle_command(c, engine, c->inbuf);
          c->inlen = 0;
          client_send_text(c, "\r\n> ");
        } else if (ch >= 0x20 && ch != 0x7f) { // basic printable, avoid locale dep
          if (c->inlen + 1 < sizeof(c->inbuf)) {
            c->inbuf[c->inlen++] = ch;
          }
        } else if ((unsigned char)ch == 0x7f || ch == '\b') {
          if (c->inlen > 0) c->inlen--;
        }
      }
    }

    // Tick engine actors minimally (no per-client actors yet)
    engine_update_all_actors(engine);
  }

  // Cleanup
  for (int i = 0; i < MUD_MAX_CLIENTS; i++) if (clients[i].sock >= 0) close(clients[i].sock);
  if (listen_fd >= 0) close(listen_fd);
  engine_destroy(engine);
  return 0;
}
