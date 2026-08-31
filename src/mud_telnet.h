#ifndef VERSE_MUD_TELNET_H
#define VERSE_MUD_TELNET_H

#include <stdbool.h>

// Run a simple BBS-style MUD server that accepts telnet clients.
// - seed: world seed id to generate / load the center world
// - world_size: cubic world size (e.g., 32)
// - port: TCP port to listen on (e.g., 2323)
// Returns 0 on clean shutdown, non-zero on error.
int run_mud_telnet_server(const char *seed, int world_size, int port);

#endif // VERSE_MUD_TELNET_H


