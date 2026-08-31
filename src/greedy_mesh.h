#ifndef GREEDY_MESH_H
#define GREEDY_MESH_H

#include <stdbool.h>
#include <stddef.h>
#include "world.h"

// Generate greedy-meshed OBJ files per voxel type present in the world.
// out_base_path is the input file path without extension; output files will be
// named like: out_base_path . "." . voxelTypeName . ".obj" next to the input.
// If a file already exists, it will not be overwritten.
// Returns true on success. Outputs the number of models (types) generated and
// total quads emitted across all models.
bool greedy_mesh_export_obj_per_voxel_type(
    const World* world,
    const char* out_base_path,
    int* out_model_count,
    size_t* out_total_quads);

#endif // GREEDY_MESH_H


