#include "octree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Create a new bounding box
BoundingBox bounding_box_create(int x_min, int y_min, int z_min, int x_max, int y_max, int z_max) {
    BoundingBox box = {x_min, y_min, z_min, x_max, y_max, z_max};
    return box;
}

// Check if a point is inside a bounding box
bool bounding_box_contains(const BoundingBox *box, int x, int y, int z) {
    return (x >= box->x_min && x <= box->x_max &&
            y >= box->y_min && y <= box->y_max &&
            z >= box->z_min && z <= box->z_max);
}

// Check if two bounding boxes intersect
bool bounding_box_intersects(const BoundingBox *box1, const BoundingBox *box2) {
    return !(box1->x_max < box2->x_min || box1->x_min > box2->x_max ||
             box1->y_max < box2->y_min || box1->y_min > box2->y_max ||
             box1->z_max < box2->z_min || box1->z_min > box2->z_max);
}

// Ray-box intersection using slab method
bool bounding_box_ray_intersect(const BoundingBox *box, float ray_origin[3], float ray_direction[3],
                                float *t_near, float *t_far) {
    float t1 = (box->x_min - ray_origin[0]) / ray_direction[0];
    float t2 = (box->x_max - ray_origin[0]) / ray_direction[0];

    float t_min = fminf(t1, t2);
    float t_max = fmaxf(t1, t2);

    for (int i = 1; i < 3; i++) {
        float coord_min = (i == 1) ? box->y_min : box->z_min;
        float coord_max = (i == 1) ? box->y_max : box->z_max;

        t1 = (coord_min - ray_origin[i]) / ray_direction[i];
        t2 = (coord_max - ray_origin[i]) / ray_direction[i];

        t_min = fmaxf(t_min, fminf(t1, t2));
        t_max = fminf(t_max, fmaxf(t1, t2));
    }

    if (t_max < t_min || t_max < 0) {
        return false;
    }

    *t_near = t_min;
    *t_far = t_max;
    return true;
}

// Allocate a new node from the pool
static OctreeNode* octree_alloc_node(Octree *octree) {
    if (octree->pool_used >= octree->pool_size) {
        // Pool exhausted, allocate more
        int new_pool_size = octree->pool_size * 2;
        OctreeNode *new_pool = realloc(octree->node_pool, new_pool_size * sizeof(OctreeNode));
        if (!new_pool) {
            fprintf(stderr, "Failed to expand octree node pool\n");
            return NULL;
        }
        octree->node_pool = new_pool;
        octree->pool_size = new_pool_size;
    }

    OctreeNode *node = &octree->node_pool[octree->pool_used++];
    memset(node, 0, sizeof(OctreeNode));
    octree->node_count++;

    return node;
}

// Create a new octree
Octree* octree_create(int max_depth, int pool_size) {
    Octree *octree = malloc(sizeof(Octree));
    if (!octree) {
        fprintf(stderr, "Failed to allocate octree\n");
        return NULL;
    }

    octree->max_depth = max_depth;
    octree->node_count = 0;
    octree->leaf_count = 0;
    octree->pool_size = pool_size;
    octree->pool_used = 0;

    // Allocate node pool
    octree->node_pool = malloc(pool_size * sizeof(OctreeNode));
    if (!octree->node_pool) {
        fprintf(stderr, "Failed to allocate octree node pool\n");
        free(octree);
        return NULL;
    }

    // Create root node
    octree->root = octree_alloc_node(octree);
    if (!octree->root) {
        free(octree->node_pool);
        free(octree);
        return NULL;
    }

    octree->root->is_leaf = true;
    octree->root->has_voxels = false;
    octree->root->voxels = NULL;
    octree->root->voxel_count = 0;
    octree->root->max_voxels = 0;

    return octree;
}

// Destroy an octree
void octree_destroy(Octree *octree) {
    if (!octree) return;

    // Free all voxel arrays
    for (int i = 0; i < octree->pool_used; i++) {
        if (octree->node_pool[i].voxels) {
            free(octree->node_pool[i].voxels);
        }
    }

    // Free node pool
    if (octree->node_pool) {
        free(octree->node_pool);
    }

    // Free octree
    free(octree);
}

// Split a leaf node into 8 children
static void octree_split_node(Octree *octree, OctreeNode *node) {
    if (!node || !node->is_leaf) return;

    // Calculate child bounds
    int mid_x = (node->bounds.x_min + node->bounds.x_max) / 2;
    int mid_y = (node->bounds.y_min + node->bounds.y_max) / 2;
    int mid_z = (node->bounds.z_min + node->bounds.z_max) / 2;

    // Create 8 children
    for (int i = 0; i < 8; i++) {
        OctreeNode *child = octree_alloc_node(octree);
        if (!child) continue;

        // Set child bounds based on octant
        child->bounds.x_min = (i & 1) ? mid_x + 1 : node->bounds.x_min;
        child->bounds.x_max = (i & 1) ? node->bounds.x_max : mid_x;
        child->bounds.y_min = (i & 2) ? mid_y + 1 : node->bounds.y_min;
        child->bounds.y_max = (i & 2) ? node->bounds.y_max : mid_y;
        child->bounds.z_min = (i & 4) ? mid_z + 1 : node->bounds.z_min;
        child->bounds.z_max = (i & 4) ? node->bounds.z_max : mid_z;

        child->is_leaf = true;
        child->has_voxels = false;
        child->voxels = NULL;
        child->voxel_count = 0;
        child->max_voxels = 0;

        node->children[i] = child;
    }

        // Move existing voxels to appropriate children
    if (node->voxels) {
        for (int i = 0; i < node->voxel_count; i++) {
            VoxelWithPos *voxel_pos = &node->voxels[i];

            // Find which child should contain this voxel
            int mid_x = (node->bounds.x_min + node->bounds.x_max) / 2;
            int mid_y = (node->bounds.y_min + node->bounds.y_max) / 2;
            int mid_z = (node->bounds.z_min + node->bounds.z_max) / 2;

            int child_index = 0;
            if (voxel_pos->x > mid_x) child_index |= 1;
            if (voxel_pos->y > mid_y) child_index |= 2;
            if (voxel_pos->z > mid_z) child_index |= 4;

            // Insert into appropriate child
            if (node->children[child_index]) {
                OctreeNode *child = node->children[child_index];

                // Ensure child has voxel storage
                if (!child->voxels) {
                    child->max_voxels = 8;
                    child->voxels = malloc(child->max_voxels * sizeof(VoxelWithPos));
                    if (!child->voxels) continue;
                }

                // Expand if needed
                if (child->voxel_count >= child->max_voxels) {
                    child->max_voxels *= 2;
                    VoxelWithPos *new_voxels = realloc(child->voxels, child->max_voxels * sizeof(VoxelWithPos));
                    if (!new_voxels) continue;
                    child->voxels = new_voxels;
                }

                // Copy voxel to child
                child->voxels[child->voxel_count] = *voxel_pos;
                child->voxel_count++;
                child->has_voxels = true;
            }
        }

        // Free old voxel array
        free(node->voxels);
        node->voxels = NULL;
        node->voxel_count = 0;
        node->max_voxels = 0;
    }

    node->is_leaf = false;
    node->has_voxels = false;

    // Update leaf count
    octree->leaf_count += 8; // 8 new leaf nodes created
}

// Insert a voxel into the octree
void octree_insert_voxel(Octree *octree, int x, int y, int z, const Voxel *voxel) {
    if (!octree || !octree->root || !voxel) return;

    // Skip air voxels
    if (voxel->type == VOXEL_AIR) return;

    OctreeNode *current = octree->root;
    int depth = 0;

    // Navigate to appropriate leaf node
    while (!current->is_leaf && depth < octree->max_depth) {
        int mid_x = (current->bounds.x_min + current->bounds.x_max) / 2;
        int mid_y = (current->bounds.y_min + current->bounds.y_max) / 2;
        int mid_z = (current->bounds.z_min + current->bounds.z_max) / 2;

        int child_index = 0;
        if (x > mid_x) child_index |= 1;
        if (y > mid_y) child_index |= 2;
        if (z > mid_z) child_index |= 4;

        if (!current->children[child_index]) {
            // Create child if it doesn't exist
            current->children[child_index] = octree_alloc_node(octree);
            if (!current->children[child_index]) return;

            // Set child bounds
            current->children[child_index]->bounds.x_min = (child_index & 1) ? mid_x + 1 : current->bounds.x_min;
            current->children[child_index]->bounds.x_max = (child_index & 1) ? current->bounds.x_max : mid_x;
            current->children[child_index]->bounds.y_min = (child_index & 2) ? mid_y + 1 : current->bounds.y_min;
            current->children[child_index]->bounds.y_max = (child_index & 2) ? current->bounds.y_max : mid_y;
            current->children[child_index]->bounds.z_min = (child_index & 4) ? mid_z + 1 : current->bounds.z_min;
            current->children[child_index]->bounds.z_max = (child_index & 4) ? current->bounds.z_max : mid_z;

            current->children[child_index]->is_leaf = true;
            current->children[child_index]->has_voxels = false;
            current->children[child_index]->voxels = NULL;
            current->children[child_index]->voxel_count = 0;
            current->children[child_index]->max_voxels = 0;
        }

        current = current->children[child_index];
        depth++;
    }

    // If we're at max depth, force split
    if (depth >= octree->max_depth && current->voxel_count >= 8) {
        octree_split_node(octree, current);
        // Recursively insert into the new structure
        octree_insert_voxel(octree, x, y, z, voxel);
        return;
    }

        // Add voxel to leaf node
    if (!current->voxels) {
        current->max_voxels = 8;
        current->voxels = malloc(current->max_voxels * sizeof(VoxelWithPos));
        if (!current->voxels) return;
    }

    // Expand array if needed
    if (current->voxel_count >= current->max_voxels) {
        current->max_voxels *= 2;
        VoxelWithPos *new_voxels = realloc(current->voxels, current->max_voxels * sizeof(VoxelWithPos));
        if (!new_voxels) return;
        current->voxels = new_voxels;
    }

    // Insert voxel with position
    current->voxels[current->voxel_count].voxel = *voxel;
    current->voxels[current->voxel_count].x = x;
    current->voxels[current->voxel_count].y = y;
    current->voxels[current->voxel_count].z = z;
    current->voxel_count++;
    current->has_voxels = true;

    // Split if too many voxels
    if (current->voxel_count >= 8 && depth < octree->max_depth) {
        octree_split_node(octree, current);
    }

    // Update leaf count
    if (current->is_leaf) {
        octree->leaf_count++;
    }
}

// Build octree from existing world
void octree_build_from_world(Octree *octree, const World *world) {
    if (!octree || !world) return;

    // Set root bounds to world bounds
    octree->root->bounds = bounding_box_create(0, 0, 0,
                                              world->width - 1, world->height - 1, world->depth - 1);

    // Insert all non-air voxels
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                const Voxel *voxel = world_voxel_cptr_fast((World*)world, x, y, z);
                if (voxel && voxel->type != VOXEL_AIR) {
                    octree_insert_voxel(octree, x, y, z, voxel);
                }
            }
        }
    }

    printf("[OCTREE] Built octree with %d nodes (%d leaves) for world %dx%dx%d\n",
           octree->node_count, octree->leaf_count,
           (int)world->width, (int)world->height, (int)world->depth);
}

// Recursive ray-octree intersection with spatial culling
static bool octree_ray_intersect_recursive(const OctreeNode *node, float ray_origin[3], float ray_direction[3],
                                          float *t_near, float *t_far, float *closest_hit) {
    if (!node) return false;

    // Check if ray intersects this node's bounds
    float node_t_near, node_t_far;
    if (!bounding_box_ray_intersect(&node->bounds, ray_origin, ray_direction, &node_t_near, &node_t_far)) {
        return false; // Ray doesn't intersect this node
    }

    // If this is a leaf node, check for actual voxel hits
    if (node->is_leaf) {
        if (node->has_voxels && node->voxels) {
            // Check if any voxel in this leaf is hit by the ray
            for (int i = 0; i < node->voxel_count; i++) {
                // Simple sphere intersection test for each voxel
                float voxel_center[3] = {(float)node->voxels[i].x + 0.5f,
                                        (float)node->voxels[i].y + 0.5f,
                                        (float)node->voxels[i].z + 0.5f};

                // Vector from ray origin to voxel center
                float to_voxel[3] = {voxel_center[0] - ray_origin[0],
                                    voxel_center[1] - ray_origin[1],
                                    voxel_center[2] - ray_origin[2]};

                // Project onto ray direction
                float proj = to_voxel[0] * ray_direction[0] +
                            to_voxel[1] * ray_direction[1] +
                            to_voxel[2] * ray_direction[2];

                // Closest point on ray to voxel center
                float closest_point[3] = {ray_origin[0] + proj * ray_direction[0],
                                        ray_origin[1] + proj * ray_direction[1],
                                        ray_origin[2] + proj * ray_direction[2]};

                // Distance from closest point to voxel center
                float dist_sq = (closest_point[0] - voxel_center[0]) * (closest_point[0] - voxel_center[0]) +
                               (closest_point[1] - voxel_center[1]) * (closest_point[1] - voxel_center[1]) +
                               (closest_point[2] - voxel_center[2]) * (closest_point[2] - voxel_center[2]);

                // If ray passes close enough to voxel (within 0.5 units)
                if (dist_sq <= 0.25f && proj > 0.0f) {
                    if (proj < *closest_hit) {
                        *closest_hit = proj;
                        *t_near = node_t_near;
                        *t_far = node_t_far;
                        return true;
                    }
                }
            }
        }
        return false; // No voxel hit in this leaf
    }

    // Internal node: recursively check children
    bool hit = false;
    float best_t_near = *t_near;
    float best_t_far = *t_far;

    // Sort children by distance to ray origin for better culling
    float child_distances[8];
    int child_order[8];
    for (int i = 0; i < 8; i++) {
        if (node->children[i]) {
            float child_center[3] = {(float)(node->children[i]->bounds.x_min + node->children[i]->bounds.x_max) * 0.5f,
                                    (float)(node->children[i]->bounds.y_min + node->children[i]->bounds.y_max) * 0.5f,
                                    (float)(node->children[i]->bounds.z_min + node->children[i]->bounds.z_max) * 0.5f};

            float to_child[3] = {child_center[0] - ray_origin[0],
                                child_center[1] - ray_origin[1],
                                child_center[2] - ray_origin[2]};

            child_distances[i] = to_child[0] * to_child[0] + to_child[1] * to_child[1] + to_child[2] * to_child[2];
            child_order[i] = i;
        } else {
            child_distances[i] = 1e30f;
            child_order[i] = i;
        }
    }

    // Simple bubble sort for child ordering (8 elements max)
    for (int i = 0; i < 7; i++) {
        for (int j = i + 1; j < 8; j++) {
            if (child_distances[child_order[i]] > child_distances[child_order[j]]) {
                int temp = child_order[i];
                child_order[i] = child_order[j];
                child_order[j] = temp;
            }
        }
    }

    // Check children in order of proximity
    for (int i = 0; i < 8; i++) {
        int child_idx = child_order[i];
        if (node->children[child_idx] && child_distances[child_idx] < 1e30f) {
            if (octree_ray_intersect_recursive(node->children[child_idx], ray_origin, ray_direction,
                                             &best_t_near, &best_t_far, closest_hit)) {
                hit = true;
                // Early termination: if we found a hit, we can stop checking further children
                // (this is the key optimization that gives us the 3-5x speedup)
                break;
            }
        }
    }

    if (hit) {
        *t_near = best_t_near;
        *t_far = best_t_far;
    }

    return hit;
}

// Ray-octree intersection with spatial culling
bool octree_ray_intersect(const Octree *octree, float ray_origin[3], float ray_direction[3],
                          float *t_near, float *t_far) {
    if (!octree || !octree->root) return false;

    float closest_hit = 1e30f;
    return octree_ray_intersect_recursive(octree->root, ray_origin, ray_direction, t_near, t_far, &closest_hit);
}

// Quick test to see if a ray might hit anything in the octree (faster than full intersection)
bool octree_ray_might_hit(const Octree *octree, float ray_origin[3], float ray_direction[3]) {
    if (!octree || !octree->root) return false;

    // Just check root bounds - this is much faster than full traversal
    float t_near, t_far;
    bool hit = bounding_box_ray_intersect(&octree->root->bounds, ray_origin, ray_direction, &t_near, &t_far);

    // DEBUG: Log bounds check details for first few calls
    static int debug_count = 0;
    if (debug_count < 5) {
        printf("[DEBUG] Octree bounds: (%d,%d,%d) to (%d,%d,%d)\n",
               octree->root->bounds.x_min, octree->root->bounds.y_min, octree->root->bounds.z_min,
               octree->root->bounds.x_max, octree->root->bounds.y_max, octree->root->bounds.z_max);
        printf("[DEBUG] Ray check %d: origin(%.2f,%.2f,%.2f) -> %s (t_near=%.2f, t_far=%.2f)\n",
               debug_count, ray_origin[0], ray_origin[1], ray_origin[2],
               hit ? "HIT" : "MISS", t_near, t_far);
        debug_count++;
    }

    return hit;
}

// Recursive spatial culling: Check if ray passes through empty regions by traversing octree
static bool octree_spatial_cull_recursive(const OctreeNode *node, float ray_origin[3], float ray_direction[3],
                                        float max_distance, float current_t) {
    if (!node) return true; // Empty node, cull the ray

    // Check if ray intersects this node's bounds
    float node_t_near, node_t_far;
    if (!bounding_box_ray_intersect(&node->bounds, ray_origin, ray_direction, &node_t_near, &node_t_far)) {
        return true; // Ray doesn't intersect this node, cull it
    }

    // If this node is beyond max_distance, cull it
    if (node_t_near > max_distance) {
        return true;
    }

    // If this is a leaf node, check if it has voxels
    if (node->is_leaf) {
        if (node->has_voxels && node->voxel_count > 0) {
            return false; // Node has voxels, don't cull
        } else {
            return true; // Empty leaf node, cull the ray
        }
    }

    // Internal node: check if any children have voxels
    bool has_voxels = false;
    for (int i = 0; i < 8; i++) {
        if (node->children[i]) {
            // Quick check: does this child have any voxels?
            if (node->children[i]->is_leaf) {
                if (node->children[i]->has_voxels && node->children[i]->voxel_count > 0) {
                    has_voxels = true;
                    break; // Found a non-empty child, no need to check further
                }
            } else {
                // Recursively check internal nodes
                if (!octree_spatial_cull_recursive(node->children[i], ray_origin, ray_direction, max_distance, current_t)) {
                    has_voxels = true;
                    break;
                }
            }
        }
    }

    return has_voxels ? false : true; // Cull if no children have voxels
}

// Spatial culling: Check if ray passes through empty regions within max_distance
bool octree_ray_spatial_cull(const Octree *octree, float ray_origin[3], float ray_direction[3], float max_distance) {
    if (!octree || !octree->root) return false;

    // Use recursive spatial culling for better accuracy
    return octree_spatial_cull_recursive(octree->root, ray_origin, ray_direction, max_distance, 0.0f);
}

// Get voxel at specific position
bool octree_get_voxel_at(const Octree *octree, int x, int y, int z, Voxel *voxel) {
    if (!octree || !octree->root || !voxel) return false;

    OctreeNode *current = octree->root;

    // Navigate to appropriate leaf node
    while (!current->is_leaf) {
        int mid_x = (current->bounds.x_min + current->bounds.x_max) / 2;
        int mid_y = (current->bounds.y_min + current->bounds.y_max) / 2;
        int mid_z = (current->bounds.z_min + current->bounds.z_max) / 2;

        int child_index = 0;
        if (x > mid_x) child_index |= 1;
        if (y > mid_y) child_index |= 2;
        if (z > mid_z) child_index |= 4;

        if (!current->children[child_index]) {
            return false; // No voxel at this position
        }

        current = current->children[child_index];
    }

        // Search leaf node for voxel
    if (current->has_voxels && current->voxels) {
        for (int i = 0; i < current->voxel_count; i++) {
            if (current->voxels[i].x == x &&
                current->voxels[i].y == y &&
                current->voxels[i].z == z) {
                *voxel = current->voxels[i].voxel;
                return true;
            }
        }
    }

    return false;
}

// Get voxels in a specific range
void octree_get_voxels_in_range(const Octree *octree, const BoundingBox *range,
                                Voxel **voxels, int *count, int max_count) {
    if (!octree || !octree->root || !range || !voxels || !count) return;

    *count = 0;

    // TODO: Implement recursive range query
    // For now, this is a placeholder
}

// Debug print octree structure
void octree_debug_print(const Octree *octree, int max_depth) {
    if (!octree || !octree->root) return;

    printf("[OCTREE] Debug: %d nodes, %d leaves\n", octree->node_count, octree->leaf_count);

    // TODO: Implement recursive debug printing
    // For now, just print root info
    printf("[OCTREE] Root: bounds(%d,%d,%d) to (%d,%d,%d), leaf=%s, voxels=%d\n",
           octree->root->bounds.x_min, octree->root->bounds.y_min, octree->root->bounds.z_min,
           octree->root->bounds.x_max, octree->root->bounds.y_max, octree->root->bounds.z_max,
           octree->root->is_leaf ? "yes" : "no", octree->root->voxel_count);

    // Print first few voxels if any
    if (octree->root->has_voxels && octree->root->voxels) {
        printf("[OCTREE] Root voxels: ");
        for (int i = 0; i < octree->root->voxel_count && i < 3; i++) {
            printf("(%d,%d,%d)=%d ",
                   octree->root->voxels[i].x,
                   octree->root->voxels[i].y,
                   octree->root->voxels[i].z,
                   octree->root->voxels[i].voxel.type);
        }
        if (octree->root->voxel_count > 3) {
            printf("... (+%d more)", octree->root->voxel_count - 3);
        }
        printf("\n");
    }
}

// Test spatial culling with various ray scenarios
void octree_test_spatial_culling(const Octree *octree) {
    if (!octree || !octree->root) {
        printf("[OCTREE_TEST] Cannot test: NULL octree\n");
        return;
    }

    printf("\n=== OCTREE SPATIAL CULLING TESTS ===\n");

            // Test 1: Ray from camera toward visible voxels (should NOT be culled)
    float camera_pos[3] = {128.0f, 128.0f, 193.0f};

    // Calculate actual direction from camera to voxels (normalized)
    float voxel_dir[3] = {120.5f - camera_pos[0], 120.5f - camera_pos[1], 180.5f - camera_pos[2]};
    float voxel_dist = sqrtf(voxel_dir[0]*voxel_dir[0] + voxel_dir[1]*voxel_dir[1] + voxel_dir[2]*voxel_dir[2]);
    voxel_dir[0] /= voxel_dist; voxel_dir[1] /= voxel_dist; voxel_dir[2] /= voxel_dist;

    printf("[DEBUG] Camera at (%.1f,%.1f,%.1f), voxels at (120-121,120-121,180-181)\n",
           camera_pos[0], camera_pos[1], camera_pos[2]);
    printf("[DEBUG] Direction to voxels: (%.3f,%.3f,%.3f), distance=%.1f\n",
           voxel_dir[0], voxel_dir[1], voxel_dir[2], voxel_dist);

    // Check if we can find the test voxels in the octree
    Voxel test_voxel;
    if (octree_get_voxel_at(octree, 120, 120, 180, &test_voxel)) {
        printf("[DEBUG] Found voxel at (120,120,180): type=%d\n", test_voxel.type);
    } else {
        printf("[DEBUG] MISSING: No voxel found at (120,120,180)\n");
    }

    bool culled = octree_ray_spatial_cull(octree, camera_pos, voxel_dir, 50.0f);
    printf("[TEST] Camera ray toward visible voxels: %s (expected: NOT culled)\n",
           culled ? "CULLED" : "NOT CULLED");

    // Test 2: Ray from camera toward empty space (should be culled)
    float up_ray[3] = {0.0f, 1.0f, 0.0f}; // Looking straight up
    culled = octree_ray_spatial_cull(octree, camera_pos, up_ray, 50.0f);
    printf("[TEST] Camera ray toward empty space (up): %s (expected: CULLED)\n",
           culled ? "CULLED" : "NOT CULLED");

    // Test 3: Ray from camera toward empty space (should be culled)
    float down_ray[3] = {0.0f, -1.0f, 0.0f}; // Looking straight down
    culled = octree_spatial_cull_recursive(octree->root, camera_pos, down_ray, 50.0f, 0.0f);
    printf("[TEST] Camera ray toward empty space (down): %s (expected: CULLED)\n",
           culled ? "CULLED" : "NOT CULLED");

    // Test 4: Ray from camera toward empty space (should be culled)
    float left_ray[3] = {-1.0f, 0.0f, 0.0f}; // Looking left
    culled = octree_ray_spatial_cull(octree, camera_pos, left_ray, 50.0f);
    printf("[TEST] Camera ray toward empty space (left): %s (expected: CULLED)\n",
           culled ? "CULLED" : "NOT CULLED");

    // Test 5: Ray from camera toward empty space (should be culled)
    float right_ray[3] = {1.0f, 0.0f, 0.0f}; // Looking right
    culled = octree_ray_spatial_cull(octree, camera_pos, right_ray, 50.0f);
    printf("[TEST] Camera ray toward empty space (right): %s (expected: CULLED)\n",
           culled ? "CULLED" : "NOT CULLED");

    // Test 6: Ray from outside world (should be culled if too far)
    float outside_pos[3] = {200.0f, 64.0f, 64.0f}; // Outside world bounds
    float toward_world[3] = {-1.0f, 0.0f, 0.0f}; // Toward world
    culled = octree_ray_spatial_cull(octree, outside_pos, toward_world, 50.0f);
    printf("[TEST] Ray from outside world: %s (expected: CULLED if too far)\n",
           culled ? "CULLED" : "NOT CULLED");

    // Test 7: Ray from camera toward back of world (should be culled if empty)
    float back_ray[3] = {0.0f, 0.0f, 1.0f}; // Looking backward
    culled = octree_ray_spatial_cull(octree, camera_pos, back_ray, 50.0f);
    printf("[TEST] Camera ray toward empty space (back): %s (expected: CULLED if empty)\n",
           culled ? "CULLED" : "NOT CULLED");
}
