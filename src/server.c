// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

/* NOISE protocol implementation dependencies */
#include <noise/protocol.h>
#include <noise/protocol/constants.h>

#define PORT 9999
#define BUFFER_SIZE 4096
#define FABRIC_MAGIC 0xC0D3F33D
#define MAX_CLIENTS 32

typedef struct {
  int socket;
  struct sockaddr_in address;
  NoiseHandshakeState *handshake_state;
  NoiseCipherState *recv_cipher;
  NoiseCipherState *send_cipher;
  int handshake_complete;
} client_t;

volatile sig_atomic_t running = 1;

/* Signal handler for graceful shutdown */
void handle_signal(int sig) {
  printf("\nReceived signal %d, shutting down...\n", sig);
  running = 0;
}

/* Set non-blocking mode for socket */
int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl F_GETFL");
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl F_SETFL O_NONBLOCK");
    return -1;
  }
  return 0;
}

/* Initialize NOISE protocol for a client */
int init_noise_for_client(client_t *client) {
  NoiseProtocolId protocol_id = {
    .prefix_id = NOISE_PREFIX_STANDARD,
    .pattern_id = NOISE_PATTERN_XX,
    .cipher_id = NOISE_CIPHER_CHACHAPOLY,
    .hash_id = NOISE_HASH_BLAKE2s,
    .dh_id = NOISE_DH_CURVE25519  /* Using CURVE25519 */
  };

  NoiseBuffer prologue = {
    .data = (uint8_t*)"FABRIC",
    .size = 6,
    .max_size = 6
  };

  /* Create handshake state directly */
  if (noise_handshakestate_new_by_id(&client->handshake_state, &protocol_id, NOISE_ROLE_RESPONDER) != NOISE_ERROR_NONE) {
    fprintf(stderr, "Failed to create handshake state\n");
    return -1;
  }

  /* Set prologue */
  if (noise_handshakestate_set_prologue(client->handshake_state, prologue.data, prologue.size) != NOISE_ERROR_NONE) {
    fprintf(stderr, "Failed to set prologue\n");
    noise_handshakestate_free(client->handshake_state);
    return -1;
  }

  /* Generate keypair and start handshake */
  if (noise_handshakestate_start(client->handshake_state) != NOISE_ERROR_NONE) {
    fprintf(stderr, "Failed to start handshake\n");
    noise_handshakestate_free(client->handshake_state);
    return -1;
  }

  client->handshake_complete = 0;
  client->recv_cipher = NULL;
  client->send_cipher = NULL;

  return 0;
}

/* Process NOISE handshake message */
int process_handshake(client_t *client, uint8_t *message, size_t message_len, uint8_t *response, size_t *response_len) {
  NoiseBuffer mbuf = {
    .data = message,
    .size = message_len,
    .max_size = message_len
  };

  NoiseBuffer rbuf = {
    .data = response,
    .size = 0,
    .max_size = BUFFER_SIZE
  };

  int action = noise_handshakestate_get_action(client->handshake_state);

  if (action == NOISE_ACTION_READ_MESSAGE) {
    /* Process incoming handshake message */
    int result = noise_handshakestate_read_message(client->handshake_state, &mbuf, &rbuf);
    if (result != NOISE_ERROR_NONE) {
      fprintf(stderr, "Failed to read handshake message: %d\n", result);
      return -1;
    }

    action = noise_handshakestate_get_action(client->handshake_state);
  }

  if (action == NOISE_ACTION_WRITE_MESSAGE) {
    /* Generate outgoing handshake message */
    int result = noise_handshakestate_write_message(client->handshake_state, &mbuf, &rbuf);
    if (result != NOISE_ERROR_NONE) {
      fprintf(stderr, "Failed to write handshake message: %d\n", result);
      return -1;
    }

    *response_len = rbuf.size;
    action = noise_handshakestate_get_action(client->handshake_state);
  }

  if (action == NOISE_ACTION_SPLIT) {
    /* Handshake completed, split into cipher states */
    int result = noise_handshakestate_split(client->handshake_state, &client->send_cipher, &client->recv_cipher);
    if (result != NOISE_ERROR_NONE) {
      fprintf(stderr, "Failed to split handshake: %d\n", result);
      return -1;
    }

    noise_handshakestate_free(client->handshake_state);
    client->handshake_state = NULL;
    client->handshake_complete = 1;

    printf("NOISE handshake completed with client %d\n", client->socket);
  }

  return 0;
}

/* Process Fabric message received over NOISE encrypted channel */
int process_fabric_message(client_t *client, uint8_t *ciphertext, size_t ct_len) {
    /* Create a buffer for ciphertext, which will be modified during decryption */
    uint8_t temp_buffer[BUFFER_SIZE];
    memcpy(temp_buffer, ciphertext, ct_len);

    NoiseBuffer buffer = {
      .data = temp_buffer,
      .size = ct_len,
      .max_size = BUFFER_SIZE
    };

    /* Decrypt the message - this modifies buffer in-place */
    int result = noise_cipherstate_decrypt(client->recv_cipher, &buffer);
    if (result != NOISE_ERROR_NONE) {
      fprintf(stderr, "Failed to decrypt message: %d\n", result);
      return -1;
    }

    /* Check if this is a valid Fabric message */
    if (buffer.size < 4) {
      return 0;  /* Too short to be a Fabric message */
    }

    uint32_t magic;
    memcpy(&magic, buffer.data, 4);

    /* Convert from network byte order if necessary */
    if (magic == htonl(FABRIC_MAGIC)) {
      printf("Received Fabric message from client %d (magic: %08X)\n", client->socket, ntohl(magic));

      /* Process the Fabric message here */
      /* For now, just dump the message content */
      printf("Message content (%zu bytes):\n", buffer.size - 4);
      for (size_t i = 4; i < buffer.size; i++) {
        printf("%02X ", buffer.data[i]);
        if ((i - 3) % 16 == 0) printf("\n");
      }
      printf("\n");

      return 1;  /* Successfully processed a Fabric message */
    }

    return 0;  /* Not a Fabric message */
}

/* Send encrypted response to client */
int send_encrypted_response(client_t *client, uint8_t *plaintext, size_t pt_len) {
    /* Create a buffer for plaintext */
    NoiseBuffer buffer = {
      .data = plaintext,
      .size = pt_len,
      .max_size = BUFFER_SIZE
    };

    /* This buffer will be modified during encryption */
    uint8_t temp_buffer[BUFFER_SIZE];
    memcpy(temp_buffer, plaintext, pt_len);
    buffer.data = temp_buffer;

    /* Encrypt the message - this modifies buffer in-place */
    int result = noise_cipherstate_encrypt(client->send_cipher, &buffer);
    if (result != NOISE_ERROR_NONE) {
      fprintf(stderr, "Failed to encrypt message: %d\n", result);
      return -1;
    }

    /* Send the encrypted message */
    if (send(client->socket, buffer.data, buffer.size, 0) < 0) {
      perror("send");
      return -1;
    }

    return 0;
}

/* Clean up client resources */
void cleanup_client(client_t *client) {
  if (client->socket >= 0) {
    close(client->socket);
    client->socket = -1;
  }

  if (client->handshake_state) {
    noise_handshakestate_free(client->handshake_state);
    client->handshake_state = NULL;
  }

  if (client->send_cipher) {
    noise_cipherstate_free(client->send_cipher);
    client->send_cipher = NULL;
  }

  if (client->recv_cipher) {
    noise_cipherstate_free(client->recv_cipher);
    client->recv_cipher = NULL;
  }
}

/* Main server function, renamed from main() to run_server() */
int run_server(void) {
  int server_fd, max_fd;
  struct sockaddr_in address;
  int opt = 1;
  client_t clients[MAX_CLIENTS];
  fd_set read_fds;

  /* Initialize client array */
  for (int i = 0; i < MAX_CLIENTS; i++) {
    clients[i].socket = -1;
    clients[i].handshake_state = NULL;
    clients[i].recv_cipher = NULL;
    clients[i].send_cipher = NULL;
    clients[i].handshake_complete = 0;
  }

  /* Set up signal handlers */
  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  /* Initialize Noise library */
  if (noise_init() != NOISE_ERROR_NONE) {
    fprintf(stderr, "Failed to initialize Noise library\n");
    return 1;
  }

  /* Create server socket */
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    return 1;
  }

  /* Set socket options */
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    perror("setsockopt");
    return 1;
  }

  /* Configure server address */
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  /* Bind the socket */
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    return 1;
  }

  /* Listen for connections */
  if (listen(server_fd, 3) < 0) {
    perror("listen");
    return 1;
  }

  /* Set server socket to non-blocking */
  if (set_nonblocking(server_fd) < 0) {
    close(server_fd);
    return 1;
  }

  printf("Fabric NOISE server started on port %d\n", PORT);
  printf("Waiting for connections...\n");

  /* Main server loop */
  while (running) {
    /* Set up the file descriptor set */
    FD_ZERO(&read_fds);
    FD_SET(server_fd, &read_fds);
    max_fd = server_fd;

    /* Add active clients to the fd set */
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (clients[i].socket > 0) {
        FD_SET(clients[i].socket, &read_fds);
        if (clients[i].socket > max_fd) {
          max_fd = clients[i].socket;
        }
      }
    }

    /* Wait for activity on one of the sockets */
    struct timeval tv = {0, 100000}; /* 100ms timeout */
    int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
    
    if (activity < 0 && errno != EINTR) {
      perror("select error");
      break;
    }

    /* Handle new connections */
    if (FD_ISSET(server_fd, &read_fds)) {
      int new_socket;
      struct sockaddr_in client_addr;
      int client_addrlen = sizeof(client_addr);

      if ((new_socket = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t*)&client_addrlen)) < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("accept");
        }
      } else {
        printf("New connection, socket fd: %d, ip: %s, port: %d\n", 
             new_socket, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        /* Set the new socket to non-blocking */
        if (set_nonblocking(new_socket) < 0) {
          close(new_socket);
          continue;
        }

        /* Add the new client to the array */
        int added = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
          if (clients[i].socket < 0) {
            clients[i].socket = new_socket;
            clients[i].address = client_addr;
            
            /* Initialize NOISE for this client */
            if (init_noise_for_client(&clients[i]) < 0) {
              cleanup_client(&clients[i]);
              break;
            }
            
            added = 1;
            break;
          }
        }
        
        if (!added) {
          printf("No space for new client, connection rejected\n");
          close(new_socket);
        }
      }
    }

    /* Handle data from clients */
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (clients[i].socket > 0 && FD_ISSET(clients[i].socket, &read_fds)) {
        uint8_t buffer[BUFFER_SIZE];
        int bytes_read = recv(clients[i].socket, buffer, BUFFER_SIZE, 0);

        if (bytes_read <= 0) {
          /* Client disconnected or error */
          if (bytes_read == 0) {
            printf("Client %d disconnected\n", clients[i].socket);
          } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              perror("recv");
            }
          }

          cleanup_client(&clients[i]);
        } else {
          /* Data received */
          if (!clients[i].handshake_complete) {
            /* Still in handshake phase */
            uint8_t response[BUFFER_SIZE];
            size_t response_len = 0;

            if (process_handshake(&clients[i], buffer, bytes_read, response, &response_len) < 0) {
              cleanup_client(&clients[i]);
              continue;
            }

            /* Send handshake response if needed */
            if (response_len > 0) {
              if (send(clients[i].socket, response, response_len, 0) < 0) {
                perror("send");
                cleanup_client(&clients[i]);
                continue;
              }
            }
          } else {
            /* Handshake complete, process the encrypted message */
            if (process_fabric_message(&clients[i], buffer, bytes_read) > 0) {
              /* Send a simple acknowledgment response */
              uint8_t ack_message[8] = {0};
              *(uint32_t*)ack_message = htonl(FABRIC_MAGIC);  /* Magic bytes */
              *(uint32_t*)(ack_message + 4) = htonl(0x00000001);  /* Simple ACK */

              if (send_encrypted_response(&clients[i], ack_message, 8) < 0) {
                cleanup_client(&clients[i]);
                continue;
              }
            }
          }
        }
      }
    }
  }

  /* Clean up */
  printf("\nShutting down server...\n");

  /* Close all client connections */
  for (int i = 0; i < MAX_CLIENTS; i++) {
    cleanup_client(&clients[i]);
  }

  /* Close server socket */
  close(server_fd);

  printf("Server shutdown complete.\n");
  return 0;
}

#ifdef MAIN_SERVER
/* Main function for standalone server executable */
int main() {
  return run_server();
}
#endif
