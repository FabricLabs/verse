#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include "player.h"
#include "constants.h"

// Helper function to create directory if it doesn't exist
static bool ensure_directory_exists(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
#ifdef _WIN32
        return _mkdir(path) == 0;
#else
        return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
    }
    return true;
}

PlayerState* player_state_create(void) {
    PlayerState* state = (PlayerState*)malloc(sizeof(PlayerState));
    if (state) {
        memset(state, 0, sizeof(PlayerState));
        // Initialize with default values
        state->health = 100;
        state->x = 0;
        state->y = 0;
        state->z = 0;
        state->current_world_id[0] = '\0';
    }
    return state;
}

void player_state_destroy(PlayerState* state) {
    free(state);
}

PlayerState* load_player_state(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        return NULL;
    }

    PlayerState* state = (PlayerState*)malloc(sizeof(PlayerState));
    if (!state) {
        fclose(file);
        return NULL;
    }

    size_t read = fread(state, sizeof(PlayerState), 1, file);
    fclose(file);

    if (read != 1) {
        free(state);
        return NULL;
    }

    return state;
}

bool save_player_state(PlayerState* state, const char* filename) {
    if (!state || !filename) {
        return false;
    }

    // Ensure the directory exists
    if (!ensure_directory_exists(VERSE_STORE_DIR)) {
        return false;
    }

    FILE* file = fopen(filename, "wb");
    if (!file) {
        return false;
    }

    size_t written = fwrite(state, sizeof(PlayerState), 1, file);
    fclose(file);

    return written == 1;
} 