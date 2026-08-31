# World Module Decomposition Example

## Overview

This document demonstrates how to decompose the monolithic `world.c` file into smaller, focused modules. We start with extracting the noise functions as a complete example.

## Step 1: Identify Functions to Extract

From `world.c`, we identified these noise-related functions:
- `perlin_noise()` - 4D Perlin noise
- `simplex_noise()` - 4D Simplex noise
- `world_set_universe_noise_seed()` - Global seed management
- Related helper functions and data

## Step 2: Create Module Headers

We created:
- `world_noise.h` - Public interface for noise functions
- `world_internal.h` - Internal shared definitions
- Other module headers for future extraction

## Step 3: Extract Implementation

Created `world_noise.c` containing:
1. All noise generation functions
2. Global permutation tables
3. Seed management
4. Helper functions (fade, lerp, grad)

## Step 4: Test the Module

Created `test_decomposition.c` to verify:
- Noise functions work correctly
- Deterministic behavior maintained
- No dependencies on world.c internals

## Step 5: Update Build System

Add to Makefile:
```makefile
# Decomposition test
test-decomposition: src/test_decomposition.o src/world_noise.o $(SHA256_OBJ)
	$(CC) $(CFLAGS) -o test-decomposition src/test_decomposition.o \
	      src/world_noise.o $(SHA256_OBJ) $(LDFLAGS)

src/world_noise.o: src/world_noise.c src/world_noise.h
	$(CC) $(CFLAGS) -c src/world_noise.c -o src/world_noise.o
```

## Step 6: Gradual Migration

1. Keep original functions in world.c temporarily
2. Update one caller at a time to use new module
3. Once all callers updated, remove from world.c
4. Update world.h to include world_noise.h

## Benefits Achieved

1. **Focused Module**: Noise functions now in dedicated 350-line file
2. **Clear Interface**: Public API in world_noise.h
3. **Testable**: Can test noise functions independently
4. **Reusable**: Other projects can use just the noise module

## Next Steps

Apply same process to:
1. **World Generation**: Extract each world type generator
2. **Physics**: Extract fluid simulation, gravity
3. **Serialization**: Extract save/load functions
4. **Voxel Operations**: Extract basic voxel get/set

## Guidelines for Further Decomposition

1. **One Responsibility**: Each module should have a single, clear purpose
2. **Minimal Dependencies**: Avoid circular dependencies
3. **Clean Interfaces**: Public headers should be minimal
4. **Internal Headers**: Use world_internal.h for shared implementation
5. **Incremental Changes**: Don't break existing code during transition

## Example Usage After Decomposition

```c
// Old way (everything through world.h)
#include "world.h"
double noise = perlin_noise_2d(x, y);

// New way (specific module)
#include "world_noise.h"
double noise = perlin_noise_2d(x, y);
```

## Measuring Success

- File sizes: No file > 500 lines (except generated)
- Compilation: 50% faster incremental builds
- Testing: Can unit test each module
- Understanding: New developers can find code easily
