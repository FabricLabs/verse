#ifndef CHUNK_CONFIG_H
#define CHUNK_CONFIG_H

// Canonical chunk dimensions for meshing, culling, and streaming.
// Keep power-of-two where possible to enable bitwise math in hot paths.
#ifndef CHUNK_SIZE_X
#define CHUNK_SIZE_X 16
#endif
#ifndef CHUNK_SIZE_Y
#define CHUNK_SIZE_Y 16
#endif
#ifndef CHUNK_SIZE_Z
#define CHUNK_SIZE_Z 256
#endif

#endif // CHUNK_CONFIG_H


