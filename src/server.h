#ifndef VERSE_SERVER_H
#define VERSE_SERVER_H

// Fabric NOISE server entrypoint. If the full server is not linked,
// a stub may be provided at build time.
int run_server(void);

#endif // VERSE_SERVER_H

#ifndef SERVER_H
#define SERVER_H

/**
 * Run the Fabric NOISE server.
 *
 * Listens on the default port (9999) for incoming connections,
 * establishes encrypted communications using the NOISE protocol,
 * and handles Fabric protocol messages.
 *
 * @return 0 on successful shutdown, non-zero on error
 */
int run_server(void);

#endif // SERVER_H
