#include "world.h"

// Production stepping now runs mob AI as well as gravity. Kept as a named entry point so the
// labyrinth solver tests continue to compile against this translation unit.
void world_step_actors_extended(World *world, float dt_seconds)
{
    world_step_actors(world, dt_seconds);
}
