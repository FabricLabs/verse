#ifndef GPU_PHYSICS_H
#define GPU_PHYSICS_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"
#include "actor.h"

// Returns true if the target world-space voxel is free for movement.
// Intended to be implemented using a compute shader over the occupancy buffer;
// currently provides a CPU fallback.
bool gpu_physics_can_move_to(World *world, int x, int y, int z);

// Try to move the actor by (dx,dy,dz) with collision. Updates the actor position
// and writes VOXEL_ACTOR marker in the world when successful. Returns true if moved.
// Intended to be executed via compute; currently CPU fallback.
bool gpu_physics_try_move_actor(World *world, Actor *actor, int dx, int dy, int dz);

#endif // GPU_PHYSICS_H


