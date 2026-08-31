#ifndef UNIVERSE_CONTEXT_H
#define UNIVERSE_CONTEXT_H

#include <stdint.h>

// Minimal universe context information for World struct
// This breaks the circular dependency between world.h and universe.h
struct Universe;  // Forward declaration only

// Universe context fields that can be added to World struct
typedef struct {
    struct Universe* universe_context;
    uint64_t universe_x;
    uint64_t universe_y;
    uint64_t universe_z;
} UniverseContext;

#endif // UNIVERSE_CONTEXT_H
