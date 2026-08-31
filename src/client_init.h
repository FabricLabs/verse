/*
 * client_init.h - Initialization and cleanup for the verse client
 *
 * Handles system setup and teardown.
 */

#ifndef CLIENT_INIT_H
#define CLIENT_INIT_H

#include <stdbool.h>

// Initialize all client systems
bool client_init(int argc, char* argv[]);

// Shutdown all client systems
void client_shutdown(void);

// Check if client is initialized
bool client_is_initialized(void);

#endif // CLIENT_INIT_H
