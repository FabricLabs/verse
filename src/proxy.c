// proxy.c - Combined HTTP/WebSocket server
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jansson.h>
#include <time.h>
#include <ctype.h> // For isprint()
#include <microhttpd.h>
#include <pthread.h>
#include <stdbool.h>
#include "actor.h"
#include "world.h"
#include "constants.h"

#include "engine.h"

// Forward declarations
static int send_websocket_message(struct lws *wsi, const char *message);

#define PORT 3039
#define ASSETS_PATH "./assets"
#define STORAGE_PATH "./stores/verse"
#define WS_PATH "/services/proxy"
#define DEBUG_LEVEL LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_DEBUG
#define DEFAULT_WORLD_PATH "default.world"
#define DEFAULT_WORLD_ID "main"
#define DEFAULT_WORLD_SIZE 32

// Content Security Policy configuration
#define CSP_HEADER "Content-Security-Policy"
#define CSP_VALUE "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self' ws: wss:;"

// Maximum number of WebSocket key to actor ID mappings
#define MAX_WS_KEYS 100

static int interrupted = 0;
static struct lws_context *context = NULL;
static Engine* engine = NULL;

// Define WebSocket key to actor ID mapping structure
typedef struct {
  char ws_key[128];
  uint32_t actor_id;
  int is_active;
} WsKeyMapping;

// Array of WebSocket key mappings
static WsKeyMapping ws_key_mappings[MAX_WS_KEYS];
static int ws_key_mapping_count = 0;

// Define session data structure
struct per_session_data {
    bool has_session;
    Actor* actor;
    char actor_id[64];  // Store the actor's ID
    char ws_key[128];   // Store the sec-websocket-key for client identification
    char world_id[64];  // Store the current world ID
    time_t last_update;
};

// Define additional data structures
static double last_broadcast_time = 0.0;
#define BROADCAST_INTERVAL 1.0 // Seconds between full state broadcasts

#define SAVE_INTERVAL 60.0 // Seconds between saving engine state
static double last_save_time = 0.0;

// Forward declarations
static void store_ws_key_mapping(const char* ws_key, uint32_t actor_id);
static Actor* find_actor_by_ws_key(const char* ws_key);
static int send_new_session_info(struct lws *wsi, const char* actor_id);
static int send_world_state(struct lws *wsi, const char* world_id);
static int handle_session_start(struct lws *wsi, json_t *json, struct per_session_data *psd);
static int handle_actor_movement(struct lws *wsi, const char *message, size_t len, struct per_session_data *psd);
static int handle_world_edit(struct lws *wsi, const char *message, size_t len, struct per_session_data *psd);
static int handle_actor_interaction(struct lws *wsi, const char *message, size_t len, struct per_session_data *psd);
static void broadcast_world_state(struct lws *wsi, const char* world_id);
static int broadcast_actor_update(struct lws *wsi, Actor* actor);
static int generate_new_world(struct lws *wsi, const char *actor_id, struct per_session_data *psd, 
                            const char* world_id, const char *seed, uint32_t width, uint32_t height, uint32_t depth);
static int generate_default_world(struct lws *wsi, const char *actor_id, struct per_session_data *psd);
static int handle_character_creation(struct lws *wsi, const char *message, size_t len, struct per_session_data *psd);

// Generate a unique actor ID
static uint32_t generate_actor_id() {
    static uint32_t next_id = 1;
    return next_id++;
}

// Send a message over WebSocket
static int send_websocket_message(struct lws *wsi, const char *message) {
    size_t len = strlen(message);
    unsigned char *buf = (unsigned char *)malloc(LWS_PRE + len);
    if (!buf) {
        printf("Failed to allocate WebSocket buffer\n");
        return -1;
    }

    memcpy(buf + LWS_PRE, message, len);
    int result = lws_write(wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    free(buf);
    return result;
}

// Create a new actor with the given ID and name
static Actor* create_actor(const char* id_str, const char* name) {
    Actor* actor = (Actor*)malloc(sizeof(Actor));
    if (!actor) {
        printf("Failed to allocate memory for actor\n");
        return NULL;
    }

    // Initialize actor properties
    actor->id = (uint32_t)strtoul(id_str, NULL, 10);
    strncpy(actor->name, name, sizeof(actor->name) - 1);
    actor->name[sizeof(actor->name) - 1] = '\0';

    // Set default position and velocity
    actor->x = 0.0;
    actor->y = 0.0;
    actor->z = 0.0;
    actor->velocity_x = 0.0;
    actor->velocity_y = 0.0;
    actor->velocity_z = 0.0;

    // Set default properties
    actor->health = 100;
    actor->inventory_size = INVENTORY_DEFAULT_SLOTS;
    inventory_init(&actor->inventory, INVENTORY_DEFAULT_SLOTS);
    actor->is_active = true;
    actor->extra_data = NULL;
    memset(actor->world_id, 0, sizeof(actor->world_id));

    printf("Created new actor: %s (ID: %u)\n", actor->name, actor->id);
    return actor;
}

// Custom log function for libwebsockets
static void lws_log_emit_function(int level, const char *line) {
  printf("LWS[%d]: %s", level, line);
}

static void sigint_handler(int sig __attribute__((unused))) {
  interrupted = 1;
  if (context) {
    lws_cancel_service(context);
  }
}

// Check if assets directory exists and print contents for debugging
static void check_assets_directory(void) {
  DIR *dir = opendir(ASSETS_PATH);
  if (dir) {
    printf("Assets directory exists.\n");
    closedir(dir);
  } else {
    printf("WARNING: Assets directory '%s' could not be opened!\n", ASSETS_PATH);
    perror("opendir");
  }
}

// Get content type based on file extension
static const char* get_content_type(const char* filename) {
  const char* ext = strrchr(filename, '.');
  if (ext == NULL) {
    return "application/octet-stream";
  }

  if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
    return "text/html";
  } else if (strcmp(ext, ".css") == 0) {
    return "text/css";
  } else if (strcmp(ext, ".js") == 0) {
    return "application/javascript";
  } else if (strcmp(ext, ".json") == 0) {
    return "application/json";
  } else if (strcmp(ext, ".png") == 0) {
    return "image/png";
  } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
    return "image/jpeg";
  } else if (strcmp(ext, ".svg") == 0) {
    return "image/svg+xml";
  } else if (strcmp(ext, ".gif") == 0) {
    return "image/gif";
  } else if (strcmp(ext, ".ico") == 0) {
    return "image/x-icon";
  } else if (strcmp(ext, ".txt") == 0) {
    return "text/plain";
  } else if (strcmp(ext, ".pdf") == 0) {
    return "application/pdf";
  } else if (strcmp(ext, ".mp3") == 0) {
    return "audio/mpeg";
  } else if (strcmp(ext, ".mp4") == 0) {
    return "video/mp4";
  }

  return "application/octet-stream";
}

// Send world state to client
static int send_world_state(struct lws *wsi, const char* world_id) {
  if (!world_id || strlen(world_id) == 0) {
    printf("ERROR: Empty or NULL world_id provided to send_world_state\n");
    return -1;
  }

  printf("Preparing to send world state to world: %s\n", world_id);

  if (!engine) {
    printf("No engine available to send\n");
    return -1;
  }
  
  // Get world data from the engine (using the center world ID)
  World* world = engine_get_world(engine, world_id);
  if (!world) {
    printf("No world data available to send\n");
    return -1;
  }
  
  printf("Retrieved world with dimensions %dx%dx%d\n", world->width, world->height, world->depth);

  // Serialize the world
  printf("Serializing world data...\n");
  char* world_data = world_serialize(world);
  if (!world_data) {
    printf("Failed to serialize world data\n");
    return -1;
  }
  printf("World serialized successfully (length: %zu bytes)\n", strlen(world_data));

  // Create world state JSON response
  printf("Building world state JSON...\n");
  json_t *response = json_object();
  json_object_set_new(response, "type", json_string("WORLD_STATE"));
  json_object_set_new(response, "world_id", json_string(world_id));
  json_object_set_new(response, "world_data", json_string(world_data));

  // Add actor data
  json_t *actors_array = json_array();
  printf("Adding %d actors to world state\n", engine_get_actor_count(engine));

  // Add all actors to the response
  for (int i = 0; i < engine_get_actor_count(engine); i++) {
    Actor* actor = engine_get_actor_by_index(engine, i);
    if (actor) {
      json_t *actor_obj = json_object();
      json_object_set_new(actor_obj, "id", json_string(((ActorEntry*)actor)->id));
      json_object_set_new(actor_obj, "x", json_real(actor->x));
      json_object_set_new(actor_obj, "y", json_real(actor->y));
      json_object_set_new(actor_obj, "z", json_real(actor->z));
      json_object_set_new(actor_obj, "world_id", json_string(((ActorEntry*)actor)->world_id));
      json_array_append_new(actors_array, actor_obj);
    }
  }

  json_object_set_new(response, "actors", actors_array);

  printf("Encoding world state to JSON...\n");
  char *response_str = json_dumps(response, JSON_COMPACT);
  free(world_data);
  json_decref(response);

  if (!response_str) {
    printf("Failed to create JSON response\n");
    return -1;
  }
  printf("JSON encoded successfully (length: %zu bytes)\n", strlen(response_str));

  // Calculate buffer size for LWS protocol
  int len = strlen(response_str);
  printf("Allocating %d bytes for WebSocket message...\n", LWS_PRE + len);
  unsigned char *buf = malloc(LWS_PRE + len);
  if (!buf) {
    printf("Failed to allocate WebSocket buffer\n");
    free(response_str);
    return -1;
  }

  // Copy message to LWS buffer with proper offset
  memcpy(&buf[LWS_PRE], response_str, len);
  free(response_str);

  // Send the message
  printf("Sending world state to client (%d bytes)...\n", len);
  int result = lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
  if (result < 0) {
    printf("Failed to send world state: error code %d\n", result);
  } else {
    printf("World state sent successfully (%d bytes)\n", result);
  }
  free(buf);

  return result;
}

// Helper function to find an actor by WebSocket key
static Actor* find_actor_by_ws_key(const char* ws_key) {
  if (!engine || !ws_key || ws_key[0] == '\0') {
    return NULL;
  }

  printf("Searching for actor with WebSocket key: %s\n", ws_key);

  // Look through our mappings for this WebSocket key
  for (int i = 0; i < ws_key_mapping_count; i++) {
    if (strcmp(ws_key_mappings[i].ws_key, ws_key) == 0 && ws_key_mappings[i].is_active) {
      uint32_t actor_id = ws_key_mappings[i].actor_id;
      printf("Found mapping: WebSocket key %s -> Actor ID %u\n", ws_key, actor_id);
      
      // Get the actor from the engine
      char actor_id_str[64];
      snprintf(actor_id_str, sizeof(actor_id_str), "%u", actor_id);
      Actor* actor = engine_find_actor(engine, actor_id_str);
      if (actor) {
        printf("Found actor with ID=%s for WebSocket key: %s\n", actor_id_str, ws_key);
        return actor;
      } else {
        printf("Actor ID %s not found in engine, removing stale mapping\n", actor_id_str);
        ws_key_mappings[i].is_active = 0; // Mark as inactive
      }
    }
  }
  
  return NULL;
}

// Helper function to store a mapping between WebSocket key and actor ID
static void store_ws_key_mapping(const char* ws_key, uint32_t actor_id) {
  if (!ws_key || ws_key[0] == '\0') {
    return;
  }
  
  // First check if this key already exists
  for (int i = 0; i < ws_key_mapping_count; i++) {
    if (strcmp(ws_key_mappings[i].ws_key, ws_key) == 0) {
      // Update existing mapping
      ws_key_mappings[i].actor_id = actor_id;
      ws_key_mappings[i].is_active = 1;
      printf("Updated mapping: WebSocket key %s -> Actor ID %u\n", ws_key, actor_id);
      return;
    }
  }
  
  // Add new mapping if we have space
  if (ws_key_mapping_count < MAX_WS_KEYS) {
    strncpy(ws_key_mappings[ws_key_mapping_count].ws_key, ws_key, sizeof(ws_key_mappings[0].ws_key) - 1);
    ws_key_mappings[ws_key_mapping_count].ws_key[sizeof(ws_key_mappings[0].ws_key) - 1] = '\0';
    ws_key_mappings[ws_key_mapping_count].actor_id = actor_id;
    ws_key_mappings[ws_key_mapping_count].is_active = 1;
    printf("Added new mapping: WebSocket key %s -> Actor ID %u\n", ws_key, actor_id);
    ws_key_mapping_count++;
  } else {
    printf("WARNING: WebSocket key mapping table full, cannot add new mapping\n");
  }
}

// Send new session info to client
static int send_new_session_info(struct lws *wsi, const char* actor_id) {
  if (!actor_id || strlen(actor_id) == 0) {
    printf("ERROR: Empty or NULL actor_id provided to send_new_session_info\n");
    return -1;
  }

  // Create session info JSON response
  json_t *response = json_object();
  json_object_set_new(response, "type", json_string("SESSION_INFO"));
  json_object_set_new(response, "actor_id", json_string(actor_id));

  // Add world ID info
  if (engine && engine->center_world_id[0] != '\0') {
    json_object_set_new(response, "world_id", json_string(engine->center_world_id));
  } else {
    json_object_set_new(response, "world_id", json_string(DEFAULT_WORLD_ID));
  }

  // Convert to string
  char *response_str = json_dumps(response, JSON_COMPACT);
  json_decref(response);

  if (!response_str) {
    printf("Failed to create JSON response\n");
    return -1;
  }

  // Calculate buffer size for LWS protocol
  int len = strlen(response_str);
  unsigned char *buf = malloc(LWS_PRE + len);
  if (!buf) {
    printf("Failed to allocate WebSocket buffer\n");
    free(response_str);
    return -1;
  }

  // Copy message to LWS buffer with proper offset
  memcpy(&buf[LWS_PRE], response_str, len);
  free(response_str);

  // Send the message
  int result = lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
  free(buf);

  return result;
}

// Parse SESSION_START message and respond with world state
static int handle_session_start(struct lws *wsi, json_t *json, struct per_session_data *psd) {
    printf("Handling SESSION_START message\n");

    // Parse JSON
    json_error_t error;
    json_t *root = json_loads(json_dumps(json, JSON_COMPACT), JSON_DISABLE_EOF_CHECK, &error);
    if (!root) {
        printf("Error parsing SESSION_START JSON: %s\n", error.text);
        return -1;
    }

    // Extract actor ID
    json_t *id = json_object_get(root, "actor_id");
    if (!id || !json_is_string(id)) {
        printf("No valid actor_id in SESSION_START message\n");
        json_decref(root);
        return -1;
    }

    // Store actor ID in session data
    strncpy(psd->actor_id, json_string_value(id), sizeof(psd->actor_id) - 1);
    psd->actor_id[sizeof(psd->actor_id) - 1] = '\0';
    psd->has_session = 1;

    printf("Session started for actor %s (WebSocket Key: %s)\n",
            psd->actor_id, psd->ws_key[0] ? psd->ws_key : "unknown");

    // Check if this WebSocket key already has an associated actor
    Actor* existing_actor = find_actor_by_ws_key(psd->ws_key);
    if (existing_actor) {
        // Reuse existing actor
        psd->actor = existing_actor;
        printf("Restoring previous actor with ID: %u for WebSocket Key: %s\n", psd->actor->id, psd->ws_key);

        // Reactivate if necessary
        if (!psd->actor->is_active) {
          printf("Reactivating actor\n");
          psd->actor->is_active = 1;
        }

        // Warn if ID mismatch
        if (strcmp(psd->actor_id, ((ActorEntry*)psd->actor)->id) != 0) {
          printf("Note: Client requested actor ID %s but is being assigned existing actor ID %s\n", psd->actor_id, ((ActorEntry*)psd->actor)->id);
        }
    }

    // Send initial world state
    send_world_state(wsi, engine->center_world_id);

    json_decref(root);
    return 0;
}

// Handle actor movement message
static int handle_actor_movement(struct lws *wsi, const char *message, size_t len __attribute__((unused)), struct per_session_data *psd) {
    if (!psd->has_session || !psd->actor) {
        return -1;
    }

    // Parse JSON
    json_error_t error;
    json_t *root = json_loads(message, 0, &error);
    if (!root) {
        printf("Error parsing JSON: %s\n", error.text);
        return -1;
    }

    // Check message type
    json_t *type = json_object_get(root, "type");
    if (!type || !json_is_string(type) || strcmp(json_string_value(type), "MOVE") != 0) {
        json_decref(root);
        return -1;
    }

    // Extract movement data
    json_t *x = json_object_get(root, "x");
    json_t *y = json_object_get(root, "y");
    json_t *z = json_object_get(root, "z");
    json_t *vx = json_object_get(root, "vx");
    json_t *vy = json_object_get(root, "vy");
    json_t *vz = json_object_get(root, "vz");

    // Update actor position and velocity if values provided
    if (x && json_is_number(x)) psd->actor->x = json_number_value(x);
    if (y && json_is_number(y)) psd->actor->y = json_number_value(y);
    if (z && json_is_number(z)) psd->actor->z = json_number_value(z);
    if (vx && json_is_number(vx)) psd->actor->velocity_x = json_number_value(vx);
    if (vy && json_is_number(vy)) psd->actor->velocity_y = json_number_value(vy);
    if (vz && json_is_number(vz)) psd->actor->velocity_z = json_number_value(vz);

    // Update the actor in the engine
    if (engine) {
        engine_update_actor(engine, psd->actor, 0.1); // Use a small delta time
    }

    // Broadcast the actor update to all connected clients
    broadcast_actor_update(wsi, psd->actor);

    json_decref(root);
    return 0;
}

// Broadcast actor update to all connected clients
static int broadcast_actor_update(struct lws *wsi, Actor* actor) {
  if (!actor) return -1;

  // Create actor update JSON
  json_t *response = json_object();
  json_object_set_new(response, "type", json_string("ACTOR_UPDATE"));
  json_object_set_new(response, "id", json_integer(actor->id));
  json_object_set_new(response, "name", json_string(actor->name));
  json_object_set_new(response, "x", json_real(actor->x));
  json_object_set_new(response, "y", json_real(actor->y));
  json_object_set_new(response, "z", json_real(actor->z));
  json_object_set_new(response, "vx", json_real(actor->velocity_x));
  json_object_set_new(response, "vy", json_real(actor->velocity_y));
  json_object_set_new(response, "vz", json_real(actor->velocity_z));
  json_object_set_new(response, "world_id", json_string(actor->world_id));

  char *response_str = json_dumps(response, JSON_COMPACT);
  json_decref(response);

  if (!response_str) {
    printf("Failed to create JSON actor update\n");
    return -1;
  }

  // Calculate buffer size for LWS protocol
  int len = strlen(response_str);
  unsigned char *buf = malloc(LWS_PRE + len);
  if (!buf) {
    free(response_str);
    return -1;
  }

  // Copy message to LWS buffer with proper offset
  memcpy(&buf[LWS_PRE], response_str, len);
  free(response_str);

  // Send the message to all clients in the same protocol
  // This doesn't actually send to everyone, just schedules the callback
  lws_callback_on_writable_all_protocol(
    lws_get_context(wsi),
    lws_get_protocol(wsi)
  );

  // A proper broadcasting mechanism would require us to store the message
  // and clients list, but for this simple implementation, we'll just send to the
  // original client
  int result = lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
  free(buf);

  return result;
}

// Get current time in seconds
static double get_current_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + (ts.tv_nsec / 1000000000.0);
}

// Broadcast all actor states to all clients
static void broadcast_all_actors(struct lws *wsi) {
  if (!engine) return;

  double now = get_current_time();
  if (now - last_broadcast_time < BROADCAST_INTERVAL) {
    return; // Not time to broadcast yet
  }

  last_broadcast_time = now;

  // Create a list of all actors
  json_t *response = json_object();
  json_object_set_new(response, "type", json_string("ACTOR_LIST"));
  
  json_t *actors_array = json_array();
  for (int i = 0; i < engine_get_actor_count(engine); i++) {
    Actor* actor = engine_get_actor_by_index(engine, i);
    if (actor) {
      json_t *actor_obj = json_object();
      json_object_set_new(actor_obj, "id", json_string(((ActorEntry*)actor)->id));
      json_object_set_new(actor_obj, "x", json_real(actor->x));
      json_object_set_new(actor_obj, "y", json_real(actor->y));
      json_object_set_new(actor_obj, "z", json_real(actor->z));
      json_object_set_new(actor_obj, "world_id", json_string(((ActorEntry*)actor)->world_id));
      json_array_append_new(actors_array, actor_obj);
    }
  }

  json_object_set_new(response, "actors", actors_array);

  char *response_str = json_dumps(response, JSON_COMPACT);
  json_decref(response);

  if (!response_str) {
    printf("Failed to create actors list JSON\n");
    return;
  }

  // Send to the provided websocket (this is limited, but a starting point)
  if (wsi) {
    int len = strlen(response_str);
    unsigned char *buf = malloc(LWS_PRE + len);
    if (buf) {
      memcpy(&buf[LWS_PRE], response_str, len);
      lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
      free(buf);
    }
  }

  free(response_str);
}

// Handle world edit message
static int handle_world_edit(struct lws *wsi, const char *message, size_t len __attribute__((unused)), struct per_session_data *psd) {
  if (!psd->has_session || !psd->actor || !engine) {
    return -1;
  }

  // Parse JSON
  json_error_t error;
  json_t *root = json_loads(message, 0, &error);
  if (!root) {
    printf("Error parsing JSON: %s\n", error.text);
    return -1;
  }

  // Check message type
  json_t *type = json_object_get(root, "type");
  if (!type || !json_is_string(type) || strcmp(json_string_value(type), "WORLD_EDIT") != 0) {
    json_decref(root);
    return -1;
  }

  // Get world ID (default to actor's current world)
  json_t *world_id_json = json_object_get(root, "world_id");
  const char* world_id = psd->actor->world_id; // Default to actor's current world
  if (world_id_json && json_is_string(world_id_json)) {
    world_id = json_string_value(world_id_json);
  }

  // Make sure the world exists
  World* world = engine_get_world(engine, world_id);
  if (!world) {
    printf("Invalid world ID: %s\n", world_id);
    json_decref(root);
    return -1;
  }

  // Get coordinates
  json_t *x_json = json_object_get(root, "x");
  json_t *y_json = json_object_get(root, "y");
  json_t *z_json = json_object_get(root, "z");
  json_t *voxel_type_json = json_object_get(root, "voxel_type");

  if (!x_json || !y_json || !z_json || !voxel_type_json ||
    !json_is_integer(x_json) || !json_is_integer(y_json) || 
    !json_is_integer(z_json) || !json_is_integer(voxel_type_json)) {
    printf("Missing or invalid coordinates or voxel type\n");
    json_decref(root);
    return -1;
  }

  uint32_t x = json_integer_value(x_json);
  uint32_t y = json_integer_value(y_json);
  uint32_t z = json_integer_value(z_json);
  VoxelType voxel_type = json_integer_value(voxel_type_json);

  // Make sure the voxel type is valid
  if (voxel_type < 0 || voxel_type >= VOXEL_COUNT) {
    printf("Invalid voxel type: %d\n", voxel_type);
    json_decref(root);
    return -1;
  }

  // Update the world
  if (world_set_voxel(world, x, y, z, voxel_type)) {
    printf("Updated voxel at (%u, %u, %u) to type %d in world %s\n", 
            x, y, z, voxel_type, world_id);

    // Create notification message
    json_t *response = json_object();
    json_object_set_new(response, "type", json_string("WORLD_EDIT_NOTIFY"));
    json_object_set_new(response, "actor_id", json_string(psd->actor_id));
    json_object_set_new(response, "world_id", json_string(world_id));
    json_object_set_new(response, "x", json_integer(x));
    json_object_set_new(response, "y", json_integer(y));
    json_object_set_new(response, "z", json_integer(z));
    json_object_set_new(response, "voxel_type", json_integer(voxel_type));

    char *response_str = json_dumps(response, JSON_COMPACT);
    json_decref(response);

    if (response_str) {
      // Send notification to all clients
      int len = strlen(response_str);
      unsigned char *buf = malloc(LWS_PRE + len);
      if (buf) {
        memcpy(&buf[LWS_PRE], response_str, len);
        lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
        free(buf);
      }
      free(response_str);
    }
  } else {
    printf("Failed to update voxel at (%u, %u, %u) in world %s\n", 
            x, y, z, world_id);
  }

  json_decref(root);
  return 0;
}

// Handle actor interaction message
static int handle_actor_interaction(struct lws *wsi, const char *message, size_t len __attribute__((unused)), struct per_session_data *psd) {
    printf("Handling actor interaction message\n");

    // Parse JSON
    json_error_t error;
    json_t *root = json_loads(message, JSON_DISABLE_EOF_CHECK, &error);
    if (!root) {
        printf("Error parsing actor interaction JSON: %s\n", error.text);
        return -1;
    }

    // Extract interaction type
    json_t *type = json_object_get(root, "type");
    if (!type || !json_is_string(type)) {
        printf("No valid type in actor interaction message\n");
        json_decref(root);
        return -1;
    }

    const char* type_str = json_string_value(type);
    if (strcmp(type_str, "INTERACT") != 0) {
        printf("Invalid interaction type: %s\n", type_str);
        json_decref(root);
        return -1;
    }

    // Extract target actor ID
    json_t *target = json_object_get(root, "target_id");
    if (!target || !json_is_string(target)) {
        printf("No valid target_id in actor interaction message\n");
        json_decref(root);
        return -1;
    }

    // Extract interaction data
    json_t *data = json_object_get(root, "data");
    if (!data || !json_is_object(data)) {
        printf("No valid data object in actor interaction message\n");
        json_decref(root);
        return -1;
    }

    // Process interaction
    if (psd->actor) {
        // Update actor state based on interaction
        // TODO: Implement actual interaction logic
        printf("Actor %s interacting with %s\n", psd->actor_id, json_string_value(target));
        broadcast_actor_update(wsi, psd->actor);
    }

    json_decref(root);
    return 0;
}

// Handle world generation request
static int handle_world_generation(struct lws *wsi, const char *message, size_t len __attribute__((unused)), struct per_session_data *psd) {
  printf("Starting world generation...\n");
  printf("Message to parse (%zu bytes): %s\n", len, message);

  // Check if engine is available
  if (!engine) {
    printf("Error: Engine not available for world generation\n");
    
    // Send error response even if engine is not available
    char response[256];
    snprintf(response, sizeof(response), 
             "{\"type\":\"ERROR\",\"message\":\"Server engine not available\"}");
    lws_write(wsi, (unsigned char *)response, strlen(response), LWS_WRITE_TEXT);
    
    return -1;
  }

  // Try to parse the JSON, but continue even if parsing fails
  json_error_t error;
  json_t *root = json_loads(message, JSON_DISABLE_EOF_CHECK, &error);
  if (!root) {
    printf("Error parsing JSON in world generation: %s at position %d\n", 
           error.text, error.position);
    printf("Context: %s\n", error.source);
    
    // Try to extract the actor ID directly with string search
    const char *id_start = strstr(message, "\"id\":");
    char actor_id_extracted[64] = {0};
    
    if (id_start) {
      id_start += 5; // Skip over "id":
      // Skip whitespace
      while (*id_start && (*id_start == ' ' || *id_start == '\t')) id_start++;
      
      // If it's a string value
      if (*id_start == '"') {
        id_start++; // Skip the opening quote
        int i = 0;
        while (*id_start && *id_start != '"' && i < 63) {
          actor_id_extracted[i++] = *id_start++;
        }
        actor_id_extracted[i] = '\0';
        printf("Extracted actor ID: %s\n", actor_id_extracted);
      }
    }
    
    // Create a minimal world with default settings
    const char *actor_id = actor_id_extracted[0] ? actor_id_extracted : NULL;
    return generate_default_world(wsi, actor_id, psd);
  }

  // Log the received message for debugging
  char *msg_dump = json_dumps(root, JSON_COMPACT);
  if (msg_dump) {
    printf("Received world generation request: %s\n", msg_dump);
    free(msg_dump);
  }

  // Check message type
  json_t *type = json_object_get(root, "type");
  if (!type || !json_is_string(type) || strcmp(json_string_value(type), "GENERATE_WORLD_REQUEST") != 0) {
    printf("Error: Invalid message type for world generation. Expected GENERATE_WORLD_REQUEST\n");
    json_decref(root);
    return -1;
  }
  
  printf("Confirmed GENERATE_WORLD_REQUEST message\n");

  // Get actor ID if provided
  json_t *id_json = json_object_get(root, "id");
  const char *actor_id = NULL;
  if (id_json && json_is_string(id_json)) {
    actor_id = json_string_value(id_json);
    printf("Request from actor ID: %s\n", actor_id);
    
    // If we don't have an actor but the message provides an ID, try to find or create one
    if (!psd->actor && engine) {
      uint32_t numeric_id = (uint32_t)atoi(actor_id);
      printf("Looking for actor with ID: %u\n", numeric_id);
      
      // Try to find existing actor
      char actor_id_str[64];
      snprintf(actor_id_str, sizeof(actor_id_str), "%u", numeric_id);
      psd->actor = engine_find_actor(engine, actor_id_str);
      
      // If actor doesn't exist, create a new one
      if (!psd->actor) {
        printf("Actor not found, creating new actor with ID: %s\n", actor_id);
        psd->actor = engine_add_actor(engine, actor_id, 
                                     0.0, 0.0, 0.0,    // Default position (x,y,z)
                                     DEFAULT_WORLD_ID); // Default world ID
        if (psd->actor) {
          printf("Created new actor with ID: %u\n", psd->actor->id);
          strncpy(psd->actor_id, actor_id, sizeof(psd->actor_id) - 1);
          psd->actor_id[sizeof(psd->actor_id) - 1] = '\0';
          
          // Establish session automatically
          psd->has_session = 1;
          printf("Automatically established session for new actor\n");
          
          // Associate this WebSocket key with the actor
          if (psd->ws_key[0]) {
            store_ws_key_mapping(psd->ws_key, psd->actor->id);
          }
        } else {
          printf("Failed to create actor, continuing with world generation anyway\n");
        }
      } else {
        printf("Found existing actor with ID: %u\n", psd->actor->id);
        strncpy(psd->actor_id, actor_id, sizeof(psd->actor_id) - 1);
        psd->actor_id[sizeof(psd->actor_id) - 1] = '\0';
        
        // Establish session automatically if not already established
        if (!psd->has_session) {
          psd->has_session = 1;
          printf("Automatically established session for existing actor\n");
        }
        
        // Associate this WebSocket key with the actor
        if (psd->ws_key[0]) {
          store_ws_key_mapping(psd->ws_key, psd->actor->id);
        }
      }
    }
  } else {
    printf("No actor ID provided in the request\n");
    
    // If no actor ID provided and no existing session, create a default session
    if (!psd->has_session && !psd->actor) {
      // Generate a default actor ID if needed
      char default_actor_id[64];
      snprintf(default_actor_id, sizeof(default_actor_id), "actor_%ld", (long)time(NULL));
      
      printf("Creating default actor with ID: %s\n", default_actor_id);
      psd->actor = engine_add_actor(engine, default_actor_id, 
                                   0.0, 0.0, 0.0,    // Default position (x,y,z)
                                   DEFAULT_WORLD_ID); // Default world ID
      if (psd->actor) {
        printf("Created default actor with ID: %u\n", psd->actor->id);
        strncpy(psd->actor_id, default_actor_id, sizeof(psd->actor_id) - 1);
        psd->actor_id[sizeof(psd->actor_id) - 1] = '\0';
        
        // Establish session automatically
        psd->has_session = 1;
        printf("Automatically established session for default actor\n");
        
        // Associate this WebSocket key with the actor
        if (psd->ws_key[0]) {
          store_ws_key_mapping(psd->ws_key, psd->actor->id);
        }
      } else {
        printf("Failed to create default actor, continuing with world generation anyway\n");
      }
    }
  }

  // Get world ID (use center world if not specified)
  json_t *world_id_json = json_object_get(root, "world_id");
  const char* world_id = DEFAULT_WORLD_ID; // Default to center world
  if (world_id_json && json_is_string(world_id_json)) {
    world_id = json_string_value(world_id_json);
  }
  printf("Using world ID: %s\n", world_id);

  // Get seed if provided, otherwise use a random one
  json_t *seed_json = json_object_get(root, "seed");
  const char *seed = "random";
  if (seed_json && json_is_string(seed_json)) {
    seed = json_string_value(seed_json);
  }
  printf("Using seed: %s\n", seed);

  // Get dimensions if provided, otherwise use defaults
  json_t *width_json = json_object_get(root, "width");
  json_t *height_json = json_object_get(root, "height");
  json_t *depth_json = json_object_get(root, "depth");

  uint32_t width = 32;  // Default size
  uint32_t height = 32; // Default size
  uint32_t depth = 32;  // Default size

  if (width_json && json_is_integer(width_json)) {
    width = json_integer_value(width_json);
  }
  if (height_json && json_is_integer(height_json)) {
    height = json_integer_value(height_json);
  }
  if (depth_json && json_is_integer(depth_json)) {
    depth = json_integer_value(depth_json);
  }
  printf("World dimensions: %ux%ux%u\n", width, height, depth);

  int result = generate_new_world(wsi, actor_id, psd, world_id, seed, width, height, depth);
  json_decref(root);
  return result;
}

// Generate a new world with specified parameters
static int generate_new_world(struct lws *wsi, const char *actor_id, struct per_session_data *psd, 
                             const char* world_id, const char *seed, uint32_t width, uint32_t height, uint32_t depth) {
  // Remove existing world if any
  if (engine_get_world(engine, world_id)) {
    printf("Removing existing world with ID: %s\n", world_id);
    engine_remove_world_entry(engine, world_id);
  }

  // Create new world
  printf("Creating new world...\n");
  World* new_world = world_create(width, height, depth);
  if (!new_world) {
    printf("Failed to create new world\n");

    // Send error response if world creation fails
    char response[256];
    snprintf(response, sizeof(response), 
             "{\"type\":\"ERROR\",\"message\":\"Failed to create new world\"}");
    lws_write(wsi, (unsigned char *)response, strlen(response), LWS_WRITE_TEXT);

    return -1;
  }

  // Generate world with provided seed
  printf("Generating world with seed: %s\n", seed);
  world_generate(new_world, seed);

  // Create a file path for the world if needed
  char world_path[256];
  if (strcmp(world_id, engine->center_world_id) == 0) {
    strncpy(world_path, DEFAULT_WORLD_PATH, sizeof(world_path));
  } else {
    snprintf(world_path, sizeof(world_path), "%s.world", world_id);
  }

  // Add world to engine
  if (!engine_add_world_entry(engine, world_id, new_world)) {
    fprintf(stderr, "Failed to add world to engine\n");
    world_destroy(new_world);
    return -1;
  }

  // If this is the center world, update the center world ID
  if (strcmp(world_id, engine->center_world_id) == 0 || engine->center_world_id[0] == '\0') {
    strncpy(engine->center_world_id, world_id, sizeof(engine->center_world_id) - 1);
    engine->center_world_id[sizeof(engine->center_world_id) - 1] = '\0';
  }

  // Save the new world
  printf("Saving world to disk...\n");
  if (engine_save_worlds(engine)) {
    printf("World saved successfully\n");
  } else {
    printf("Failed to save world to disk\n");
  }

  printf("Generated new world with ID: %s and seed: %s\n", world_id, seed);

  // Determine which actor ID to use for the world state response
  const char* response_actor_id = psd->actor_id;
  if (actor_id && strlen(actor_id) > 0) {
    response_actor_id = actor_id;
  }

  // Send world state to client
  printf("Sending world state to client (actor ID: %s)...\n", response_actor_id);
  int result = -1;
  if (response_actor_id && strlen(response_actor_id) > 0) {
    result = send_world_state(wsi, response_actor_id);
  } else {
    printf("WARNING: No actor ID available for world state, skipping send_world_state\n");
  }
  
  if (result < 0) {
    printf("Failed to send world state to client\n");
  } else {
    printf("World state sent successfully (%d bytes)\n", result);
  }

  // Create notification message
  printf("Preparing WORLD_GENERATED notification...\n");
  json_t *response = json_object();
  json_object_set_new(response, "type", json_string("WORLD_GENERATED"));
  json_object_set_new(response, "world_id", json_string(world_id));
  json_object_set_new(response, "seed", json_string(seed));
  json_object_set_new(response, "width", json_integer(width));
  json_object_set_new(response, "height", json_integer(height));
  json_object_set_new(response, "depth", json_integer(depth));
  
  // Include actor ID if available
  if (response_actor_id && strlen(response_actor_id) > 0) {
    json_object_set_new(response, "actor_id", json_string(response_actor_id));
  }

  char *response_str = json_dumps(response, JSON_COMPACT);
  json_decref(response);

  if (response_str) {
    // Send notification to all clients
    int len = strlen(response_str);
    unsigned char *buf = malloc(LWS_PRE + len);
    if (buf) {
      memcpy(&buf[LWS_PRE], response_str, len);
      printf("Sending WORLD_GENERATED notification (%d bytes)...\n", len);
      result = lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
      if (result < 0) {
        printf("Failed to send WORLD_GENERATED notification\n");
      } else {
        printf("WORLD_GENERATED notification sent successfully (%d bytes)\n", result);
      }
      free(buf);
    } else {
      printf("Failed to allocate buffer for notification\n");
    }
    free(response_str);
  } else {
    printf("Failed to create JSON response for notification\n");
  }

  printf("World generation completed successfully\n");
  return 0;
}

// Generate a default world when the JSON parsing fails
static int generate_default_world(struct lws *wsi, const char *actor_id, struct per_session_data *psd) {
  printf("Generating default world due to JSON parsing failure\n");
  return generate_new_world(wsi, actor_id, psd, DEFAULT_WORLD_ID, "default", 32, 32, 32);
}

// Broadcast world state to all connected clients
static void broadcast_world_state(struct lws *wsi, const char* world_id) {
    if (!world_id || !engine) return;

    // Find the world
    World* world = engine_get_world(engine, world_id);
    if (!world) return;

    // Create JSON response
    json_t *response = json_object();
    json_object_set_new(response, "type", json_string("WORLD_STATE"));
    json_object_set_new(response, "world_id", json_string(world_id));
    
    // Add world data
    json_t *world_data = json_object();
    json_object_set_new(world_data, "width", json_integer(world->width));
    json_object_set_new(world_data, "height", json_integer(world->height));
    json_object_set_new(world_data, "depth", json_integer(world->depth));
    json_object_set_new(response, "world", world_data);

    // Convert to string
    char *response_str = json_dumps(response, JSON_COMPACT);
    if (response_str) {
        // Send to client
        lws_write(wsi, (unsigned char*)response_str, strlen(response_str), LWS_WRITE_TEXT);
        free(response_str);
    }

    json_decref(response);
}

// Handle character creation message
static int handle_character_creation(struct lws *wsi, const char *message, size_t len, struct per_session_data *psd) {
    json_error_t error;
    json_t *root = json_loads(message, 0, &error);
    if (!root) {
        printf("Failed to parse character creation message: %s\n", error.text);
        return -1;
    }

    // Get the character name from the message
    json_t *content = json_object_get(root, "content");
    if (!content || !json_is_object(content)) {
        printf("Character creation message missing content object\n");
        json_decref(root);
        return -1;
    }

    json_t *name = json_object_get(content, "name");
    if (!name || !json_is_string(name)) {
        printf("Character creation message missing name\n");
        json_decref(root);
        return -1;
    }

    const char *character_name = json_string_value(name);
    printf("Creating character with name: %s\n", character_name);

    // Generate a unique actor ID for the character
    uint32_t actor_id = generate_actor_id();
    char actor_id_str[64];
    snprintf(actor_id_str, sizeof(actor_id_str), "%u", actor_id);

    // Store the WebSocket key mapping
    char ws_key[128];
    lws_get_peer_simple(wsi, ws_key, sizeof(ws_key));
    store_ws_key_mapping(ws_key, actor_id);

    // Initialize the session data
    psd->has_session = true;
    psd->actor = create_actor(actor_id_str, character_name);
    strncpy(psd->actor_id, actor_id_str, sizeof(psd->actor_id) - 1);
    strncpy(psd->ws_key, ws_key, sizeof(psd->ws_key) - 1);
    psd->last_update = time(NULL);

    // Create response message
    json_t *response = json_object();
    json_object_set_new(response, "type", json_string("CHARACTER_CREATED"));
    json_object_set_new(response, "actor_id", json_string(actor_id_str));
    json_object_set_new(response, "name", json_string(character_name));

    // Send the response
    char *response_str = json_dumps(response, JSON_COMPACT);
    if (response_str) {
        send_websocket_message(wsi, response_str);
        free(response_str);
    }

    json_decref(response);
    json_decref(root);

    // Generate a default world for the new character
    return generate_default_world(wsi, actor_id_str, psd);
}

// WebSocket callback for received messages
static int callback_verse(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len) {
    struct per_session_data *psd = (struct per_session_data *)user;
    char clean_message[4096];
    size_t clean_len;
    char client_name[50];
    char buf[256];

    switch (reason) {
        case LWS_CALLBACK_HTTP: {
            // Get client info for debugging
            lws_get_peer_simple(wsi, client_name, sizeof(client_name));

            // Get the request URI
            if (lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI) < 0) {
                return -1;
            }

            printf("HTTP: Request from %s: %s\n", client_name, buf);

            // Check if this is the WebSocket endpoint
            if (strncmp(buf, WS_PATH, strlen(WS_PATH)) == 0) {
                printf("HTTP: WebSocket endpoint accessed via HTTP\n");
                return -1;
            }

            // Skip leading slash if present
            const char *path = buf;
            if (path[0] == '/') {
                path++;
            }

            // Handle empty path (root)
            if (strlen(path) == 0) {
                path = "index.html";
            }

            // Protect against directory traversal
            if (strstr(path, "..") != NULL) {
                lws_return_http_status(wsi, HTTP_STATUS_FORBIDDEN, NULL);
                return -1;
            }

            // Build full path
            char abs_path[1024];
            snprintf(abs_path, sizeof(abs_path), "%s/%s", ASSETS_PATH, path);
            printf("HTTP: Trying to serve file: %s\n", abs_path);

            // Check if file exists
            struct stat st;
            if (stat(abs_path, &st) != 0) {
                printf("HTTP: File not found: %s\n", abs_path);
                lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, NULL);
                return -1;
            }

            // Send file with proper MIME type
            const char *mime = get_content_type(abs_path);
            printf("HTTP: Serving %s as %s\n", abs_path, mime);

            // Use direct file serving with completion tracking
            if (lws_serve_http_file(wsi, abs_path, mime, NULL, 0) < 0) {
                return -1;
            }
            return 0;  // Return 0 to keep connection alive
        }

        case LWS_CALLBACK_HTTP_FILE_COMPLETION:
            printf("HTTP: File transfer complete\n");
            return -1;  // Close connection after file transfer

        case LWS_CALLBACK_ESTABLISHED:
            lws_get_peer_simple(wsi, client_name, sizeof(client_name));
            printf("WS: Connection established from %s\n", client_name);
            
            // Initialize session data
            if (psd) {
                memset(psd, 0, sizeof(struct per_session_data));
                printf("WS: New session initialized for %s at %ld\n",
                         client_name, (long)time(NULL));
            }
            return 0;

        case LWS_CALLBACK_RECEIVE: {
            lws_get_peer_simple(wsi, client_name, sizeof(client_name));
            printf("WS: Message from %s (%zu bytes)\n", client_name, len);

            // Create a clean copy of the message to parse
            char *clean_message = malloc(len + 1);
            if (!clean_message) {
                printf("Memory allocation failed for message processing\n");
                return -1;
            }
            
            // Copy and null-terminate the message
            memcpy(clean_message, in, len);
            clean_message[len] = '\0';
            
            // Remove any trailing special characters (CR, LF, nulls, etc.)
            size_t clean_len = len;
            while (clean_len > 0 && 
                   (clean_message[clean_len-1] == '\r' || 
                    clean_message[clean_len-1] == '\n' || 
                    clean_message[clean_len-1] == '\0' ||
                    !isprint(clean_message[clean_len-1]))) {
                clean_message[--clean_len] = '\0';
            }
            
            printf("WS: Cleaned message (%zu bytes): %s\n", clean_len, clean_message);
            
            // Parse the JSON to determine the message type
            json_error_t error;
            json_t *root = json_loads(clean_message, JSON_DISABLE_EOF_CHECK, &error);
            int result = 0;
            
            if (root) {
                // Get the message type
                json_t *type = json_object_get(root, "type");
                if (type && json_is_string(type)) {
                    const char *type_str = json_string_value(type);
                    printf("WS: Received message type: '%s'\n", type_str);
                    
                    // Handle different message types
                    if (strcmp(type_str, "SESSION_START") == 0) {
                        result = handle_session_start(wsi, root, psd);
                    }
                    else if (strcmp(type_str, "MOVE") == 0 && psd->has_session) {
                        result = handle_actor_movement(wsi, clean_message, clean_len, psd);
                    }
                    else if (strcmp(type_str, "INTERACT") == 0 && psd->has_session) {
                        result = handle_actor_interaction(wsi, clean_message, clean_len, psd);
                    }
                    else if (strcmp(type_str, "EDIT") == 0 && psd->has_session) {
                        result = handle_world_edit(wsi, clean_message, clean_len, psd);
                    }
                    else if (strcmp(type_str, "GENERATE_WORLD_REQUEST") == 0) {
                        printf("WS: Handling world generation request\n");
                        result = handle_world_generation(wsi, clean_message, clean_len, psd);
                    }
                    else if (strcmp(type_str, "CREATE_CHARACTER") == 0) {
                        printf("WS: Handling character creation request\n");
                        result = handle_character_creation(wsi, clean_message, clean_len, psd);
                    }
                    else {
                        printf("WS: Unknown message type: %s\n", type_str);
                        result = -1;
                    }
                } else {
                    printf("WS: Message missing 'type' field\n");
                    result = -1;
                }
                
                json_decref(root);
            }
            else {
                printf("WS: Failed to parse JSON: %s at position %d\n", 
                       error.text, error.position);
                result = -1;
            }
            
            free(clean_message);
            
            // Request a callback when we can write to send any updates
            if (result == 0) {
                lws_callback_on_writable(wsi);
            }
            return result;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            // Handle any pending writes for the session
            if (psd && psd->has_session) {
                // Broadcast world state updates if needed
                broadcast_world_state(wsi, psd->world_id);
                
                // Broadcast actor updates if needed
                Actor* actor = find_actor_by_ws_key(psd->ws_key);
                if (actor) {
                    broadcast_actor_update(wsi, actor);
                }
            }
            return 0;
        }

        case LWS_CALLBACK_CLOSED:
            lws_get_peer_simple(wsi, client_name, sizeof(client_name));
            printf("WS: Connection closed from %s (WebSocket Key: %s)\n",
                   client_name, psd && psd->ws_key[0] ? psd->ws_key : "unknown");
            return 0;

        default:
            return lws_callback_http_dummy(wsi, reason, user, in, len);
    }
}

// Custom mount options for static files
static const struct lws_protocol_vhost_options extra_mimetypes[] = {
  { NULL, NULL, "text/html", "html" },
  { NULL, NULL, "text/css", "css" },
  { NULL, NULL, "application/javascript", "js" },
  { NULL, NULL, "application/json", "json" },
  { NULL, NULL, "image/png", "png" },
  { NULL, NULL, "image/jpeg", "jpg" },
  { NULL, NULL, "image/svg+xml", "svg" },
  { NULL, NULL, NULL, NULL }
};

// Protocol definitions
static struct lws_protocols protocols[] = {
    {
        "verse",                // name
        callback_verse,         // callback
        sizeof(struct per_session_data), // per_session_data_size
        4096,                  // rx_buffer_size
        0,                     // id
        NULL,                  // user
        4096                   // tx_packet_size
    },
    {
        NULL, NULL, 0, 0, 0, NULL, 0   // terminator
    }
};

// Save engine state periodically
static void save_engine_state() {
  if (!engine) return;

  double now = get_current_time();
  if (now - last_save_time < SAVE_INTERVAL) {
    return; // Not time to save yet
  }

  printf("Saving engine state...\n");
  last_save_time = now;

  // Save worlds
  if (engine_save_worlds(engine)) {
    printf("Worlds saved successfully\n");
  } else {
    printf("Failed to save worlds\n");
  }

  // Save actors
  if (engine_save_actors(engine, "actors.dat")) {
    printf("Actors saved successfully at %.1f\n", now);
  } else {
    printf("Failed to save actors\n");
  }
}

int main(int argc, char **argv) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    int port = PORT;  // Use the PORT constant instead of hardcoded value
    int opts = 0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        }
    }

    // Setup signal handlers
    signal(SIGINT, sigint_handler);

    // Initialize libwebsockets
    lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO, lws_log_emit_function);

    memset(&info, 0, sizeof info);

    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = opts;

    // Mount options for serving static files
    struct lws_http_mount mount = {
        .mount_next = NULL,           // linked-list "next"
        .mountpoint = "/",            // mountpoint URL
        .origin = ASSETS_PATH,        // serve from dir
        .def = "index.html",          // default filename
        .protocol = NULL,
        .cgienv = NULL,
        .extra_mimetypes = NULL,
        .interpret = NULL,
        .cgi_timeout = 0,
        .cache_max_age = 3600,        // Enable caching
        .auth_mask = 0,
        .cache_reusable = 1,          // Enable cache reuse
        .cache_revalidate = 0,
        .cache_intermediaries = 0,
        .origin_protocol = LWSMPRO_FILE,  // files in a dir
        .mountpoint_len = 1,          // char count
        .basic_auth_login_file = NULL,
    };

    info.mounts = &mount;

    // Create libwebsockets context
    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Failed to create libwebsocket context\n");
        return -1;
    }

    printf("Starting server on port %d...\n", port);
    printf("Serving static files from %s\n", ASSETS_PATH);
    printf("WebSocket endpoint: ws://localhost:%d%s\n", port, WS_PATH);

    // Initialize engine
    engine = engine_create();
    if (!engine) {
        lwsl_err("Failed to create engine\n");
        lws_context_destroy(context);
        return -1;
    }

    // Load or create default world
    if (!engine_load_center_world(engine)) {
        World* default_world = world_create(DEFAULT_WORLD_SIZE, DEFAULT_WORLD_SIZE, DEFAULT_WORLD_SIZE);
        if (!default_world) {
            fprintf(stderr, "Failed to create default world\n");
            engine_destroy(engine);
            lws_context_destroy(context);
            return -1;
        }

        world_generate(default_world, DEFAULT_WORLD_ID);

        if (!engine_add_world_entry(engine, DEFAULT_WORLD_ID, default_world)) {
            fprintf(stderr, "Failed to add default world to engine\n");
            world_destroy(default_world);
            engine_destroy(engine);
            lws_context_destroy(context);
            return -1;
        }

        strncpy(engine->center_world_id, DEFAULT_WORLD_ID, sizeof(engine->center_world_id) - 1);
        engine->center_world_id[sizeof(engine->center_world_id) - 1] = '\0';
    }

    // Initialize WebSocket key mapping array
    memset(ws_key_mappings, 0, sizeof(ws_key_mappings));
    ws_key_mapping_count = 0;

    // Main event loop
    while (!interrupted) {
        engine_update_all_actors(engine);
        lws_service(context, 50);
        usleep(10000); // 10ms sleep to prevent busy-waiting
    }

    // Cleanup
    if (engine) {
        engine_destroy(engine);
        engine = NULL;
    }
    lws_context_destroy(context);

    return 0;
}
