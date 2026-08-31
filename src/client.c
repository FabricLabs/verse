#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* NOISE protocol implementation dependencies */
#include <noise/protocol.h>
#include <noise/protocol/constants.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 7777
#define BUFFER_SIZE 4096
#define FABRIC_MAGIC 0xC0D3F33D

typedef struct {
    int sock;
    NoiseCipherState *send_cipher;
    NoiseCipherState *recv_cipher;
} ClientConnection;

int client_connect_and_handshake(ClientConnection *conn, const char *ip, int port, const uint8_t *expected_public_key);

/* Create a Fabric message with the magic bytes and some content */
void create_fabric_message(uint8_t *buffer, size_t *size) {
    /* Set magic bytes */
    *(uint32_t*)buffer = htonl(FABRIC_MAGIC);

    /* Add a sample message type */
    *(uint32_t*)(buffer + 4) = htonl(0x00000001);

    /* Add some sample content */
    const char *msg = "Hello, Fabric!";
    size_t len = strlen(msg);
    memcpy(buffer + 8, msg, len);

    *size = 8 + len;
}

// Connect to server and perform handshake
// expected_public_key will be used for server verification in future security updates
int client_connect_and_handshake(ClientConnection *conn, const char *ip, int port, const uint8_t *expected_public_key) {
    struct sockaddr_in serv_addr;
    uint8_t buffer[BUFFER_SIZE] = {0};
    conn->sock = 0;
    conn->send_cipher = NULL;
    conn->recv_cipher = NULL;

    if (noise_init() != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to initialize Noise library\n");
        return -1;
    }

    if ((conn->sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(conn->sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    printf("Connected to server at %s:%d\n", ip, port);

    NoiseProtocolId protocol_id = {
        .prefix_id = NOISE_PREFIX_STANDARD,
        .pattern_id = NOISE_PATTERN_XX,
        .cipher_id = NOISE_CIPHER_CHACHAPOLY,
        .hash_id = NOISE_HASH_BLAKE2s,
        .dh_id = NOISE_DH_CURVE25519
    };

    NoiseBuffer prologue = {
        .data = (uint8_t*)"FABRIC",
        .size = 6,
        .max_size = 6
    };

    NoiseHandshakeState *handshake_state;
    if (noise_handshakestate_new_by_id(&handshake_state, &protocol_id, NOISE_ROLE_INITIATOR) != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to create handshake state\n");
        close(conn->sock);
        return -1;
    }

    if (noise_handshakestate_set_prologue(handshake_state, prologue.data, prologue.size) != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to set prologue\n");
        noise_handshakestate_free(handshake_state);
        close(conn->sock);
        return -1;
    }

    if (noise_handshakestate_start(handshake_state) != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to start handshake\n");
        noise_handshakestate_free(handshake_state);
        close(conn->sock);
        return -1;
    }

    printf("Starting NOISE handshake...\n");
    int handshake_complete = 0;
    while (!handshake_complete) {
        int action = noise_handshakestate_get_action(handshake_state);
        if (action == NOISE_ACTION_WRITE_MESSAGE) {
            uint8_t message_out[BUFFER_SIZE];
            NoiseBuffer mbuf = {
                .data = message_out,
                .size = 0,
                .max_size = sizeof(message_out)
            };
            if (noise_handshakestate_write_message(handshake_state, NULL, &mbuf) != NOISE_ERROR_NONE) {
                fprintf(stderr, "Failed to write handshake message\n");
                break;
            }
            send(conn->sock, mbuf.data, mbuf.size, 0);
            printf("Sent handshake message (%zu bytes)\n", mbuf.size);
            int bytes_read = recv(conn->sock, buffer, BUFFER_SIZE, 0);
            if (bytes_read <= 0) {
                fprintf(stderr, "Server disconnected during handshake\n");
                break;
            }
            
            printf("Received handshake response (%d bytes)\n", bytes_read);
            
            /* Process the response */
            NoiseBuffer rbuf = {
                .data = buffer,
                .size = bytes_read,
                .max_size = bytes_read
            };
            
            if (noise_handshakestate_read_message(handshake_state, &rbuf, NULL) != NOISE_ERROR_NONE) {
                fprintf(stderr, "Failed to read handshake message from server\n");
                break;
            }
        } else if (action == NOISE_ACTION_READ_MESSAGE) {
            /* Wait for server to send a message first */
            int bytes_read = recv(conn->sock, buffer, BUFFER_SIZE, 0);
            if (bytes_read <= 0) {
                fprintf(stderr, "Server disconnected during handshake\n");
                break;
            }
            
            printf("Received handshake message (%d bytes)\n", bytes_read);
            
            /* Process the message */
            NoiseBuffer rbuf = {
                .data = buffer,
                .size = bytes_read,
                .max_size = bytes_read
            };
            
            NoiseBuffer mbuf = {
                .data = buffer,
                .size = 0,
                .max_size = BUFFER_SIZE
            };
            
            if (noise_handshakestate_read_message(handshake_state, &rbuf, &mbuf) != NOISE_ERROR_NONE) {
                fprintf(stderr, "Failed to read handshake message from server\n");
                break;
            }
            
            /* Send response if needed */
            if (mbuf.size > 0) {
                send(conn->sock, mbuf.data, mbuf.size, 0);
                printf("Sent handshake response (%zu bytes)\n", mbuf.size);
            }
        } else if (action == NOISE_ACTION_SPLIT) {
            /* Handshake completed, get the cipher objects */
            if (noise_handshakestate_split(handshake_state, &conn->send_cipher, &conn->recv_cipher) != NOISE_ERROR_NONE) {
                fprintf(stderr, "Failed to split handshake\n");
                break;
            }
            
            handshake_complete = 1;
            printf("NOISE handshake completed successfully!\n");
        } else {
            fprintf(stderr, "Unexpected handshake action: %d\n", action);
            break;
        }
    }
    
    /* Clean up handshake state */
    noise_handshakestate_free(handshake_state);
    
    if (!handshake_complete || !conn->send_cipher || !conn->recv_cipher) {
        fprintf(stderr, "Handshake failed\n");
        close(conn->sock);
        return -1;
    }
    
    /* Create a Fabric message */
    uint8_t fabric_message[BUFFER_SIZE];
    size_t message_size;
    create_fabric_message(fabric_message, &message_size);
    
    printf("Sending Fabric message with magic bytes C0D3F33D...\n");
    
    /* Create a buffer for plaintext and copy the message */
    uint8_t temp_buffer[BUFFER_SIZE];
    memcpy(temp_buffer, fabric_message, message_size);
    
    NoiseBuffer noise_buffer = {
        .data = temp_buffer,
        .size = message_size,
        .max_size = BUFFER_SIZE
    };
    
    /* Encrypt message (modifies buffer in-place) */
    if (noise_cipherstate_encrypt(conn->send_cipher, &noise_buffer) != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to encrypt message\n");
        noise_cipherstate_free(conn->send_cipher);
        noise_cipherstate_free(conn->recv_cipher);
        close(conn->sock);
        return -1;
    }
    
    /* Send the encrypted message */
    send(conn->sock, noise_buffer.data, noise_buffer.size, 0);
    printf("Sent encrypted Fabric message (%zu bytes)\n", noise_buffer.size);
    
    /* Wait for response */
    int bytes_read = recv(conn->sock, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        fprintf(stderr, "No response from server\n");
    } else {
        printf("Received encrypted response (%d bytes)\n", bytes_read);
        /* Prepare buffer for decryption */
        uint8_t decrypt_buffer[BUFFER_SIZE];
        memcpy(decrypt_buffer, buffer, bytes_read);
        NoiseBuffer recv_buffer = {
            .data = decrypt_buffer,
            .size = bytes_read,
            .max_size = BUFFER_SIZE
        };
        /* Decrypt the response */
        if (noise_cipherstate_decrypt(conn->recv_cipher, &recv_buffer) != NOISE_ERROR_NONE) {
            fprintf(stderr, "Failed to decrypt response\n");
        } else {
            /* Check if response has the Fabric magic bytes */
            uint32_t magic;
            memcpy(&magic, recv_buffer.data, 4);
            if (ntohl(magic) == FABRIC_MAGIC) {
                printf("Received valid Fabric response!\n");
                /* Print the response content */
                printf("Response content: ");
                for (size_t i = 0; i < recv_buffer.size; i++) {
                    printf("%02X ", recv_buffer.data[i]);
                }
                printf("\n");
            } else {
                printf("Response does not have valid Fabric magic bytes\n");
            }
        }
    }

    /* Subscription loop: receive messages from server */
    printf("Entering subscription mode. Waiting for messages from server...\n");
    while (1) {
        int bytes_read = recv(conn->sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            printf("Server disconnected or error occurred. Exiting subscription mode.\n");
            break;
        }
        uint8_t decrypt_buffer[BUFFER_SIZE];
        memcpy(decrypt_buffer, buffer, bytes_read);
        NoiseBuffer recv_buffer = {
            .data = decrypt_buffer,
            .size = bytes_read,
            .max_size = BUFFER_SIZE
        };
        if (noise_cipherstate_decrypt(conn->recv_cipher, &recv_buffer) != NOISE_ERROR_NONE) {
            fprintf(stderr, "Failed to decrypt incoming message\n");
            continue;
        }
        uint32_t magic;
        memcpy(&magic, recv_buffer.data, 4);
        if (ntohl(magic) == FABRIC_MAGIC) {
            printf("[Subscription] Received Fabric message: ");
            for (size_t i = 0; i < recv_buffer.size; i++) {
                printf("%02X ", recv_buffer.data[i]);
            }
            printf("\n");
        } else {
            printf("[Subscription] Received message without valid Fabric magic bytes\n");
        }
    }
    
    /* Clean up */
    noise_cipherstate_free(conn->send_cipher);
    noise_cipherstate_free(conn->recv_cipher);
    close(conn->sock);
    
    return 0;
}

int client_send_message(ClientConnection *conn, const uint8_t *msg, size_t msg_size) {
    uint8_t temp_buffer[BUFFER_SIZE];
    memcpy(temp_buffer, msg, msg_size);
    NoiseBuffer noise_buffer = {
        .data = temp_buffer,
        .size = msg_size,
        .max_size = BUFFER_SIZE
    };
    if (noise_cipherstate_encrypt(conn->send_cipher, &noise_buffer) != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to encrypt message\n");
        return -1;
    }
    if (send(conn->sock, noise_buffer.data, noise_buffer.size, 0) < 0) {
        fprintf(stderr, "Failed to send message\n");
        return -1;
    }
    return 0;
}

void client_subscribe(ClientConnection *conn) {
    uint8_t buffer[BUFFER_SIZE];
    printf("Entering subscription mode. Waiting for messages from server...\n");
    while (1) {
        int bytes_read = recv(conn->sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            printf("Server disconnected or error occurred. Exiting subscription mode.\n");
            break;
        }
        uint8_t decrypt_buffer[BUFFER_SIZE];
        memcpy(decrypt_buffer, buffer, bytes_read);
        NoiseBuffer recv_buffer = {
            .data = decrypt_buffer,
            .size = bytes_read,
            .max_size = BUFFER_SIZE
        };
        if (noise_cipherstate_decrypt(conn->recv_cipher, &recv_buffer) != NOISE_ERROR_NONE) {
            fprintf(stderr, "Failed to decrypt incoming message\n");
            continue;
        }
        uint32_t magic;
        memcpy(&magic, recv_buffer.data, 4);
        if (ntohl(magic) == FABRIC_MAGIC) {
            printf("[Subscription] Received Fabric message: ");
            for (size_t i = 0; i < recv_buffer.size; i++) {
                printf("%02X ", recv_buffer.data[i]);
            }
            printf("\n");
        } else {
            printf("[Subscription] Received message without valid Fabric magic bytes\n");
        }
    }
} 