// model_transformer.h
#ifndef MODEL_TRANSFORMER_H
#define MODEL_TRANSFORMER_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

// Model Transformer API
// This module provides utilities to import/export voxel models and
// convert 2D images to voxel planes in the in-memory World representation.

// Import a MagicaVoxel .vox file into the given world at an origin offset.
// Any voxel present in the .vox file will be mapped to a voxel type by a
// simple color heuristic (if palette is present) or to the provided fallback type.
// Returns true on success, false on failure.
bool model_transformer_load_vox(
    World* world,
    const char* vox_filepath,
    uint32_t origin_x,
    uint32_t origin_y,
    uint32_t origin_z,
    VoxelType fallback_type);

// Load a bitmap image (BMP supported by default; PNG/JPG supported if SDL_image
// is available at compile time) and stamp it as an X,Y voxel plane at fixed Z.
// Pixels with alpha < 128 (when available) are treated as air if transparent_is_air is true.
// Returns true on success, false on failure.
bool model_transformer_load_image_plane(
    World* world,
    const char* image_filepath,
    uint32_t plane_z,
    bool transparent_is_air);

// Export the world's solid voxels to a minimal MagicaVoxel .vox file.
// Note: Exports only a single SIZE/XYZI chunk with a small fixed palette
// mapped from VoxelType. Returns true on success.
bool model_transformer_export_vox(
    const World* world,
    const char* vox_filepath);

// Export an image snapshot of the world using a simple projection.
// Supported projections:
//   - "topdown": selects the highest solid voxel per (x,y) and colors it
// The output format is BMP by default. Returns true on success.
bool model_transformer_export_image(
    const World* world,
    const char* image_filepath,
    int camera_x,
    int camera_y,
    int camera_z,
    const char* projection);

// Utility: Probe MagicaVoxel .vox file dimensions (SIZE chunk) without loading.
// Returns true and fills out parameters if found.
bool model_transformer_probe_vox_dimensions(
    const char* vox_filepath,
    uint32_t* out_size_x,
    uint32_t* out_size_y,
    uint32_t* out_size_z);

// Import a Minecraft Classic/MCEdit .schematic (gzipped NBT) into the world at origin.
// Maps common block IDs to our VoxelType. Unknown IDs become VOXEL_STONE; ID 0 is VOXEL_AIR.
bool model_transformer_import_minecraft_schematic(
    World* world,
    const char* schematic_filepath,
    uint32_t origin_x,
    uint32_t origin_y,
    uint32_t origin_z);

// Probe a Minecraft .schematic (gzipped NBT) for dimensions only.
// Returns true on success and writes out width, height, length.
bool model_transformer_probe_schematic_dimensions(
    const char* schematic_filepath,
    uint16_t* out_width,
    uint16_t* out_height,
    uint16_t* out_length);

#endif // MODEL_TRANSFORMER_H


