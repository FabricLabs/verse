#ifndef WORLD_SERIALIZE_H
#define WORLD_SERIALIZE_H

#include <stdbool.h>
#include <stdint.h>

// Forward declaration
typedef struct World World;

// Save/Load operations
bool world_save(World* world, const char* filename);
bool world_save_with_metadata(World* world, const char* filename, const char* metadata);
World* world_load(const char* filename);
World* world_load_with_validation(const char* filename, bool validate_checksum);

// Binary serialization
size_t world_serialize_size(const World* world);
bool world_serialize_to_buffer(const World* world, uint8_t* buffer, size_t buffer_size);
World* world_deserialize_from_buffer(const uint8_t* buffer, size_t buffer_size);

// Text format export/import
bool world_export_json(const World* world, const char* filename);
World* world_import_json(const char* filename);

// Chunk-based operations for large worlds
typedef struct WorldChunk WorldChunk;
WorldChunk* world_get_chunk(World* world, uint32_t chunk_x, uint32_t chunk_y, uint32_t chunk_z);
bool world_save_chunk(const WorldChunk* chunk, const char* filename);
WorldChunk* world_load_chunk(const char* filename);

// Compression support
bool world_save_compressed(World* world, const char* filename);
World* world_load_compressed(const char* filename);

#endif // WORLD_SERIALIZE_H
