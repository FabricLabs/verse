/*
 * world_serialize.c - World serialization and file I/O operations
 *
 * This module handles saving and loading world data to/from disk,
 * including serialization formats and file management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "world_internal.h"
#include "world_serialize.h"
#include "world_core.h"
#include "world_voxel.h"

// Constants
// TODO: use constants.h instead
#define GRAVITY_DEFAULT 9.81f

// Convert a voxel type to hex character (expanded set, rough mapping)
static char voxel_type_to_hex(VoxelType type) {
    if (type == VOXEL_WORLD)
        return 'W';
    if (type >= VOXEL_COUNT)
        return '0';
    // Map groups into hex bins; not backwards compatible, but stable in this build
    static const char table[] = "0123456789ABCDEF";
    return table[type & 0x0F];
}

// Convert a hex character to voxel type (expanded set; lossy mapping)
static VoxelType hex_to_voxel_type(char hex) {
    if (hex == 'W')
        return VOXEL_WORLD;
    if (hex >= '0' && hex <= '9')
        return (VoxelType)(hex - '0');
    if (hex >= 'A' && hex <= 'F')
        return (VoxelType)(hex - 'A' + 10);
    if (hex >= 'a' && hex <= 'f')
        return (VoxelType)(hex - 'a' + 10);
    return VOXEL_AIR;
}

// Serialize a world to a hex string
static char* world_serialize_internal(World *world) {
    if (!world)
        return NULL;

    // Format: [version:4][width:8][height:8][depth:8][log_length:8][log_data...][voxels...]
    // Version is 4 hex chars (16 bits)
    // Each dimension is 8 hex chars (32 bits)
    // Log length is 8 hex chars (32 bits)
    // Log data is variable length
    // Each voxel is 1 hex char for type only, conditions are omitted for performance

    size_t voxel_count = (size_t)world->width * world->height * world->depth;
    size_t log_length = world->log ? strlen(world->log) : 0;

    // Calculate buffer size: header (28) + log length (8) + log data + voxels + null terminator
    size_t buffer_size = 36 + log_length + voxel_count + 1;
    char *buffer = (char *)malloc(buffer_size);

    if (!buffer)
        return NULL;

    // Write version and dimensions
    sprintf(buffer, "%04X%08X%08X%08X", world->version, world->width, world->height, world->depth);

    // Write log length and log data
    sprintf(buffer + 28, "%08X", (uint32_t)log_length);
    size_t offset = 36; // Start after header and log length

    if (world->log && log_length > 0) {
        memcpy(buffer + offset, world->log, log_length);
        offset += log_length;
    }

    // Write voxels (type only, no conditions for performance)
    for (size_t i = 0; i < voxel_count; i++) {
        buffer[offset++] = voxel_type_to_hex(world->voxels[i].type);
    }

    buffer[offset] = '\0';
    return buffer;
}

// Deserialize a world from a hex string
static World* world_deserialize_internal(const char *data) {
    if (!data || strlen(data) < 36) {
        printf("[world_deserialize] data too small (len=%zu)\n", data ? strlen(data) : 0UL);
        return NULL; // Minimum size for header + log length
    }

    // Read version and dimensions
    uint16_t version;
    uint32_t width, height, depth, log_length;
    char dim_buffer[9];

    // Extract version
    char version_buffer[5];
    strncpy(version_buffer, data, 4);
    version_buffer[4] = '\0';
    sscanf(version_buffer, "%hX", &version);

    // Extract width
    strncpy(dim_buffer, data + 4, 8);
    dim_buffer[8] = '\0';
    sscanf(dim_buffer, "%X", &width);

    // Extract height
    strncpy(dim_buffer, data + 12, 8);
    dim_buffer[8] = '\0';
    sscanf(dim_buffer, "%X", &height);

    // Extract depth
    strncpy(dim_buffer, data + 20, 8);
    dim_buffer[8] = '\0';
    sscanf(dim_buffer, "%X", &depth);

    // Extract log length
    strncpy(dim_buffer, data + 28, 8);
    dim_buffer[8] = '\0';
    sscanf(dim_buffer, "%X", &log_length);

    // Create new world
    World *world = world_create(width, height, depth);
    if (!world)
        return NULL;

    // Set the version
    world->version = version;

    // Initialize with default/empty values for fields not in the simple format
    // These would need to be added to the serialization format for full persistence
    memset(world->seed_id, 0, sizeof(world->seed_id));
    world->gravity = GRAVITY_DEFAULT;
    world->rarity = 0.5f;
    world->generation_type = WORLD_TYPE_UNKNOWN;
    world->vector_clock = 0;
    world->rng_state = 1;

    // Read log data
    size_t log_offset = 36;
    if (log_length > 0 && log_offset + log_length <= strlen(data)) {
        char *log_data = malloc(log_length + 1);
        if (log_data) {
            memcpy(log_data, data + log_offset, log_length);
            log_data[log_length] = '\0';
            world->log = log_data;
        }
    }

    // Read voxels (simplified format - just types, no conditions)
    size_t voxel_offset = log_offset + log_length;
    size_t voxel_count = (size_t)width * height * depth;
    size_t data_length = strlen(data);

    if (voxel_offset + voxel_count > data_length) {
        printf("[world_deserialize] partial voxel data (need %zu, have %zu)\n",
               voxel_offset + voxel_count, data_length);
    }

    for (size_t i = 0; i < voxel_count; i++) {
        if (voxel_offset + i < data_length) {
            world->voxels[i].type = hex_to_voxel_type(data[voxel_offset + i]);
        } else {
            // Default to air if data is truncated
            world->voxels[i].type = VOXEL_AIR;
        }
        // All conditions are initially empty
        world->voxels[i].condition_mask = 0ULL;
    }

    printf("[world_deserialize] parsed dims=%ux%ux%u voxels=%zu log_len=%u\n",
           width, height, depth, voxel_count, log_length);

    return world;
}

// Save world to file
bool world_save(World *world, const char *filename) {
    if (!world || !filename)
        return false;

    // Serialize the world
    char *data = world_serialize_internal(world);
    if (!data)
        return false;

    // Open file for writing
    FILE *file = fopen(filename, "w");
    if (!file) {
        free(data);
        return false;
    }

    // Write data
    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, file);
    free(data);
    fclose(file);

    return written == len;
}

// Load world from file
World* world_load(const char *filename) {
    if (!filename)
        return NULL;

    printf("[world_load] opening '%s'\n", filename);
    // Open file for reading
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("[world_load] fopen failed for '%s'\n", filename);
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        printf("[world_load] empty or invalid file size: %ld\n", file_size);
        return NULL;
    }

    // Allocate buffer for file content
    char *buffer = (char *)malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        printf("[world_load] malloc failed (size=%ld)\n", file_size);
        return NULL;
    }

    // Read file content
    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);
    if (read_size != (size_t)file_size) {
        free(buffer);
        printf("[world_load] fread mismatch (read=%zu, expected=%ld)\n", read_size, file_size);
        return NULL;
    }
    buffer[file_size] = '\0';

    // Deserialize
    World *world = world_deserialize_internal(buffer);
    free(buffer);

    if (world) {
        // Particle emitters are built lazily on first gameplay update.
        printf("[world_load] success dims=%ux%ux%u from '%s'\n",
               world->width, world->height, world->depth, filename);
    } else {
        printf("[world_load] deserialize failed for '%s'\n", filename);
    }

    return world;
}

// Load world into existing world structure (for compatibility)
World* world_load_with_validation(const char *filename, bool validate_checksum) {
    // For now, ignore checksum validation
    (void)validate_checksum;
    return world_load(filename);
}

// Save world by seed with type-specific filename
bool world_save_with_metadata(World *world, const char *filename, const char *metadata) {
    // For now, ignore metadata
    (void)metadata;
    return world_save(world, filename);
}

// Export world to JSON format
bool world_export_json(const World *world, const char *filename) {
    if (!world || !filename)
        return false;

    FILE *file = fopen(filename, "w");
    if (!file)
        return false;

    // Write basic world info as JSON
    fprintf(file, "{\n");
    fprintf(file, "  \"version\": %u,\n", world->version);
    fprintf(file, "  \"width\": %u,\n", world->width);
    fprintf(file, "  \"height\": %u,\n", world->height);
    fprintf(file, "  \"depth\": %u,\n", world->depth);
    fprintf(file, "  \"gravity\": %.3f,\n", world->gravity);
    fprintf(file, "  \"rarity\": %.3f,\n", world->rarity);
    fprintf(file, "  \"seed\": \"%s\",\n", world->seed_id);
    fprintf(file, "  \"generation_type\": %d,\n", world->generation_type);

    // Write voxel counts by type
    fprintf(file, "  \"voxel_counts\": {\n");

    size_t counts[VOXEL_COUNT] = {0};
    size_t total = (size_t)world->width * world->height * world->depth;
    for (size_t i = 0; i < total; i++) {
        VoxelType type = world->voxels[i].type;
        if (type < VOXEL_COUNT) {
            counts[type]++;
        }
    }

    bool first = true;
    for (int i = 0; i < VOXEL_COUNT; i++) {
        if (counts[i] > 0) {
            if (!first) fprintf(file, ",\n");
            fprintf(file, "    \"%d\": %zu", i, counts[i]);
            first = false;
        }
    }

    fprintf(file, "\n  }\n");
    fprintf(file, "}\n");

    fclose(file);
    return true;
}

// Import world from JSON format
World* world_import_json(const char *filename) {
    // This is a complex operation - for now just return NULL
    // Full implementation would require a JSON parser
    (void)filename;
    return NULL;
}

// Chunk operations for large worlds
WorldChunk* world_get_chunk(World *world, uint32_t chunk_x, uint32_t chunk_y, uint32_t chunk_z) {
    // Not implemented yet
    (void)world;
    (void)chunk_x;
    (void)chunk_y;
    (void)chunk_z;
    return NULL;
}

bool world_save_chunk(const WorldChunk *chunk, const char *filename) {
    // Not implemented yet
    (void)chunk;
    (void)filename;
    return false;
}

WorldChunk* world_load_chunk(const char *filename) {
    // Not implemented yet
    (void)filename;
    return NULL;
}

// Compression support
bool world_save_compressed(World *world, const char *filename) {
    // For now, just use regular save
    return world_save(world, filename);
}

World* world_load_compressed(const char *filename) {
    // For now, just use regular load
    return world_load(filename);
}

// Get serialized size estimate
size_t world_serialize_size(const World *world) {
    if (!world)
        return 0;

    size_t voxel_count = (size_t)world->width * world->height * world->depth;
    size_t log_length = world->log ? strlen(world->log) : 0;

    // Header (36) + log data + voxels + null terminator
    return 36 + log_length + voxel_count + 1;
}

// Serialize to provided buffer
bool world_serialize_to_buffer(const World *world, uint8_t *buffer, size_t buffer_size) {
    if (!world || !buffer)
        return false;

    char *serialized = world_serialize_internal((World*)world);
    if (!serialized)
        return false;

    size_t len = strlen(serialized);
    if (len + 1 > buffer_size) {
        free(serialized);
        return false;
    }

    memcpy(buffer, serialized, len + 1);
    free(serialized);
    return true;
}

// Deserialize from buffer
World* world_deserialize_from_buffer(const uint8_t *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0)
        return NULL;

    // Ensure null termination
    char *temp = malloc(buffer_size + 1);
    if (!temp)
        return NULL;

    memcpy(temp, buffer, buffer_size);
    temp[buffer_size] = '\0';

    World *world = world_deserialize_internal(temp);
    free(temp);

    return world;
}
