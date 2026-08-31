#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include "engine.h"
#include "constants.h"
#include "world.h"

// Helper function to ensure a directory exists - will be used for world saves
// and other file operations in future updates
static bool ensure_directory_exists(const char* path) {
  struct stat st = {0};
  if (stat(path, &st) == -1) {
#ifdef _WIN32
    if (mkdir(path) == -1) {
#else
    if (mkdir(path, 0700) == -1) {
#endif
      if (errno != EEXIST) {
        return false;
      }
    }
  }

  return true;
}

Engine* engine_create(void) {
    Engine* engine = (Engine*)calloc(1, sizeof(Engine));
    if (!engine) return NULL;

    engine->world_count = 0;
    engine->actor_count = 0;
    memset(engine->center_world_id, 0, sizeof(engine->center_world_id));

    return engine;
}

void engine_destroy(Engine* engine) {
    if (!engine) return;

    // Free all worlds
    for (int i = 0; i < engine->world_count; i++) {
        if (engine->worlds[i].world) {
            world_destroy(engine->worlds[i].world);
        }
    }

    // Free the engine itself
    free(engine);
}

// Helper function to generate neighbor seed
static void generate_neighbor_seed(char* buffer, size_t buffer_size, const char* base_seed, NeighborDirection direction) {
    const char* dir_suffix = "";
    switch (direction) {
        case NEIGHBOR_NORTH: dir_suffix = "_north"; break;
        case NEIGHBOR_SOUTH: dir_suffix = "_south"; break;
        case NEIGHBOR_EAST:  dir_suffix = "_east";  break;
        case NEIGHBOR_WEST:  dir_suffix = "_west";  break;
        case NEIGHBOR_UP:    dir_suffix = "_up";    break;
        case NEIGHBOR_DOWN:  dir_suffix = "_down";  break;
    }

    snprintf(buffer, buffer_size, "%s%s", base_seed, dir_suffix);
}

bool engine_add_world_entry(Engine* engine, const char* id, World* world) {
    if (!engine || !id || !world || engine->world_count >= MAX_WORLDS) return false;

    // Check if world with this ID already exists
    for (int i = 0; i < engine->world_count; i++) {
        if (strcmp(engine->worlds[i].id, id) == 0) {
            return false;
        }
    }

    // Add new world entry
    WorldEntry* entry = &engine->worlds[engine->world_count];
    strncpy(entry->id, id, sizeof(entry->id) - 1);
    entry->id[sizeof(entry->id) - 1] = '\0';
    entry->world = world;

    // Construct save file path
    construct_world_filename(entry->save_file, sizeof(entry->save_file), id);

    engine->world_count++;
    return true;
}

World* engine_get_world(Engine* engine, const char* id) {
    if (!engine || !id) return NULL;

    for (int i = 0; i < engine->world_count; i++) {
        if (strcmp(engine->worlds[i].id, id) == 0) {
            return engine->worlds[i].world;
        }
    }

    return NULL;
}

Actor* engine_add_actor(Engine* engine, const char* id, float x, float y, float z, const char* world_id) {
    if (!engine || !id || !world_id || engine->actor_count >= MAX_ACTORS) return NULL;

    // Check if actor with this ID already exists
    for (int i = 0; i < engine->actor_count; i++) {
        if (strcmp(engine->actors[i].id, id) == 0) {
            return NULL;
        }
    }

    // Add new actor entry
    ActorEntry* actor = &engine->actors[engine->actor_count];
    strncpy(actor->id, id, sizeof(actor->id) - 1);
    actor->id[sizeof(actor->id) - 1] = '\0';
    actor->x = x;
    actor->y = y;
    actor->z = z;
    strncpy(actor->world_id, world_id, sizeof(actor->world_id) - 1);
    actor->world_id[sizeof(actor->world_id) - 1] = '\0';

    engine->actor_count++;
    return (Actor*)actor;
}

Actor* engine_find_actor(Engine* engine, const char* id) {
    if (!engine || !id) return NULL;

    for (int i = 0; i < engine->actor_count; i++) {
        if (strcmp(engine->actors[i].id, id) == 0) {
            return (Actor*)&engine->actors[i];
        }
    }

    return NULL;
}

bool engine_remove_actor(Engine* engine, const char* id) {
    if (!engine || !id) return false;

    for (int i = 0; i < engine->actor_count; i++) {
        if (strcmp(engine->actors[i].id, id) == 0) {
            // Remove actor by shifting remaining actors
            for (int j = i; j < engine->actor_count - 1; j++) {
                engine->actors[j] = engine->actors[j + 1];
            }
            engine->actor_count--;
            return true;
        }
    }

    return false;
}

void engine_update_actor(Engine* engine, Actor* actor, double delta_time) {
    if (!engine || !actor) return;

    // Simple update - just validate world reference and apply velocity
    World* world = engine_get_world(engine, ((ActorEntry*)actor)->world_id);
    if (!world) {
        // Remove actor if its world doesn't exist
        engine_remove_actor(engine, ((ActorEntry*)actor)->id);
        return;
    }

    // Apply velocity over time
    actor->x += actor->velocity_x * delta_time;
    actor->y += actor->velocity_y * delta_time;
    actor->z += actor->velocity_z * delta_time;
}

bool engine_compute_neighbors(Engine* engine, const char* world_id) {
    if (!engine || !world_id) return false;

    // Get the source world
    World* source_world = engine_get_world(engine, world_id);
    if (!source_world) return false;

    // Generate neighbors in all directions
    for (NeighborDirection dir = NEIGHBOR_NORTH; dir <= NEIGHBOR_DOWN; dir++) {
        if (!engine_generate_neighbor(engine, world_id, dir, world_id)) {
            return false;
        }
    }

    return true;
}

Actor* engine_get_actor_by_index(Engine* engine, int index) {
    if (!engine || index < 0 || index >= engine->actor_count) return NULL;
    return (Actor*)&engine->actors[index];
}

int engine_get_actor_count(Engine* engine) {
    return engine ? engine->actor_count : 0;
}

bool engine_load_actors(Engine* engine, const char* filename) {
    if (!engine || !filename) return false;

    FILE* file = fopen(filename, "rb");
    if (!file) return false;

    // Read actor count
    size_t count = 0;
    if (fread(&count, sizeof(size_t), 1, file) != 1) {
        fclose(file);
        return false;
    }

    // Validate count
    if (count > MAX_ACTORS) {
        fclose(file);
        return false;
    }

    engine->actor_count = (int)count;

    // Read actors
    if (count > 0) {
        size_t read = fread(engine->actors, sizeof(ActorEntry), count, file);
        if (read != count) {
            fclose(file);
            return false;
        }
    }

    fclose(file);
    return true;
}

bool engine_save_actors(Engine* engine, const char* filename) {
    if (!engine || !filename) return false;

    FILE* file = fopen(filename, "wb");
    if (!file) return false;

    // Write actor count
    size_t count = (size_t)engine->actor_count;
    if (fwrite(&count, sizeof(size_t), 1, file) != 1) {
        fclose(file);
        return false;
    }

    // Write actors
    if (count > 0) {
        size_t written = fwrite(engine->actors, sizeof(ActorEntry), count, file);
        if (written != count) {
            fclose(file);
            return false;
        }
    }

    fclose(file);
    return true;
}

bool engine_load_center_world(Engine* engine) {
    if (!engine || !engine->center_world_id[0]) return false;

    World* world = engine_get_world(engine, engine->center_world_id);
    if (!world) {
        // Try to load from file
        char filename[256];
        construct_world_filename(filename, sizeof(filename), engine->center_world_id);

        FILE* file = fopen(filename, "r");
        if (file) {
            // Get file size
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            rewind(file);

            // Read file content
            char* buffer = (char*)malloc(file_size + 1);
            if (buffer) {
                size_t read_size = fread(buffer, 1, file_size, file);
                buffer[read_size] = '\0';

                // TODO: Implement world deserialization
                // world = world_deserialize(buffer);
                free(buffer);

                // For now, create a new world instead
                world = world_create(64, 16, 64);
                if (world) {
                    world_generate(world, engine->center_world_id);
                    return engine_add_world_entry(engine, engine->center_world_id, world);
                }
            }
            fclose(file);
        }
    }

    return world != NULL;
}

bool engine_save_worlds(Engine* engine) {
    if (!engine) return false;

    bool success = true;
    for (int i = 0; i < engine->world_count; i++) {
        WorldEntry* entry = &engine->worlds[i];
        FILE* file = fopen(entry->save_file, "w");
        if (file) {
            // TODO: Implement world serialization
            // char* serialized = world_serialize(entry->world);
            // if (serialized) {
            //     fprintf(file, "%s", serialized);
            //     free(serialized);
            // } else {
            //     success = false;
            // }
            fprintf(file, "# World save placeholder\n");
            fclose(file);
        } else {
            success = false;
        }
    }

    return success;
}

bool engine_remove_world_entry(Engine* engine, const char* id) {
    if (!engine || !id) return false;

    for (int i = 0; i < engine->world_count; i++) {
        if (strcmp(engine->worlds[i].id, id) == 0) {
            // Free the world
            world_destroy(engine->worlds[i].world);

            // Remove the entry by shifting remaining entries
            for (int j = i; j < engine->world_count - 1; j++) {
                engine->worlds[j] = engine->worlds[j + 1];
            }
            engine->world_count--;
            return true;
        }
    }

    return false;
}

bool engine_update_all_actors(Engine* engine) {
    if (!engine) return false;

    // Simple update - just validate world references
    for (int i = 0; i < engine->actor_count; i++) {
        ActorEntry* actor = &engine->actors[i];
        if (!engine_get_world(engine, actor->world_id)) {
            // Remove actor if its world doesn't exist
            engine_remove_actor(engine, actor->id);
            i--; // Adjust index since we removed an actor
        }
    }

    return true;
}

bool engine_generate_neighbor(Engine* engine, const char* source_world_id, NeighborDirection direction, const char* seed) {
    if (!engine || !source_world_id || !seed) return false;

    // Get the source world to match dimensions
    World* source_world = engine_get_world(engine, source_world_id);
    if (!source_world) return false;

    // Generate a unique ID for the neighbor world based on source and direction
    char neighbor_id[256];
    generate_neighbor_seed(neighbor_id, sizeof(neighbor_id), seed, direction);

    // Create and generate the neighbor world
    World* neighbor = world_create(source_world->width, source_world->height, source_world->depth);
    if (!neighbor) return false;

    world_generate(neighbor, neighbor_id);

    // Add the neighbor world to the engine
    if (!engine_add_world_entry(engine, neighbor_id, neighbor)) {
        world_destroy(neighbor);
        return false;
    }

    return true;
}

World* engine_get_neighbor(Engine* engine, const char* world_id, NeighborDirection direction) {
    if (!engine || !world_id) return NULL;

    // Get the source world
    World* source_world = engine_get_world(engine, world_id);
    if (!source_world) return NULL;

    // Generate the neighbor ID based on direction
    char neighbor_id[256];
    generate_neighbor_seed(neighbor_id, sizeof(neighbor_id), world_id, direction);

    // Return the neighbor world if it exists
    return engine_get_world(engine, neighbor_id);
}

bool engine_save_neighbors(Engine* engine, const char* world_id) {
    if (!engine || !world_id) return false;

    // Get the source world
    World* source_world = engine_get_world(engine, world_id);
    if (!source_world) return false;

    bool success = true;

    // Save all existing neighbor worlds
    for (NeighborDirection dir = NEIGHBOR_NORTH; dir <= NEIGHBOR_DOWN; dir++) {
        World* neighbor = engine_get_neighbor(engine, world_id, dir);
        if (neighbor) {
            char filename[256];
            construct_world_filename(filename, sizeof(filename), ((WorldEntry*)neighbor)->id);
            if (!world_save(neighbor, filename)) {
                success = false;
            }
        }
    }

    return success;
}
