#ifndef OCTREE_H
#define OCTREE_H

#include <stdint.h>
#include <stdbool.h>
#include "world.h"

// Voxel with position information for octree storage
typedef struct {
    Voxel voxel;
    int x, y, z;
} VoxelWithPos;

// Bounding box for octree nodes
typedef struct {
    int x_min, y_min, z_min;
    int x_max, y_max, z_max;
} BoundingBox;

// Octree node structure
typedef struct OctreeNode {
    BoundingBox bounds;
    bool is_leaf;
    bool has_voxels;

    // For leaf nodes: direct voxel storage with positions
    VoxelWithPos *voxels;
    int voxel_count;
    int max_voxels;

    // For internal nodes: 8 children
    struct OctreeNode *children[8];

    // Memory management
    struct OctreeNode *next_free; // For object pooling
} OctreeNode;

// Octree root structure
typedef struct {
    OctreeNode *root;
    int max_depth;
    int node_count;
    int leaf_count;

    // Memory pool for efficient allocation
    OctreeNode *node_pool;
    int pool_size;
    int pool_used;
} Octree;

// Function declarations
Octree* octree_create(int max_depth, int pool_size);
void octree_destroy(Octree *octree);
void octree_insert_voxel(Octree *octree, int x, int y, int z, const Voxel *voxel);
void octree_build_from_world(Octree *octree, const World *world);
bool octree_ray_intersect(const Octree *octree, float ray_origin[3], float ray_direction[3],
                          float *t_near, float *t_far);
bool octree_ray_might_hit(const Octree *octree, float ray_origin[3], float ray_direction[3]);
bool octree_ray_spatial_cull(const Octree *octree, float ray_origin[3], float ray_direction[3], float max_distance);
bool octree_get_voxel_at(const Octree *octree, int x, int y, int z, Voxel *voxel);
void octree_get_voxels_in_range(const Octree *octree, const BoundingBox *range,
                                Voxel **voxels, int *count, int max_count);
void octree_debug_print(const Octree *octree, int max_depth);
void octree_test_spatial_culling(const Octree *octree);

// Utility functions
BoundingBox bounding_box_create(int x_min, int y_min, int z_min, int x_max, int y_max, int z_max);
bool bounding_box_contains(const BoundingBox *box, int x, int y, int z);
bool bounding_box_intersects(const BoundingBox *box1, const BoundingBox *box2);
bool bounding_box_ray_intersect(const BoundingBox *box, float ray_origin[3], float ray_direction[3],
                                float *t_near, float *t_far);

#endif // OCTREE_H
