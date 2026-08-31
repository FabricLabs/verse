#ifndef CLIENT_H
#define CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <noise/protocol.h>

#define BUFFER_SIZE 4096
#define FABRIC_MAGIC 0xC0D3F33D

typedef struct {
  int sock;
  NoiseCipherState *send_cipher;
  NoiseCipherState *recv_cipher;
} ClientConnection;

/**
 * Connect and perform NOISE handshake with the server.
 * If expected_public_key is not NULL, verify the server's static public key matches.
 * expected_public_key must be 32 bytes (Curve25519 public key).
 */
int client_connect_and_handshake(ClientConnection *conn, const char *ip, int port, const uint8_t *expected_public_key);
int client_send_message(ClientConnection *conn, const uint8_t *msg, size_t msg_size);
void client_subscribe(ClientConnection *conn);

#endif // CLIENT_H 