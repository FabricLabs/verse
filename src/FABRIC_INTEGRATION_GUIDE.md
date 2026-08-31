# Fabric Project Integration Guide

## Overview

The fabric project (`~/fabric`) contains several components that can be integrated with the verse project:

### 1. Secure Random (`fabric/src/secure_random.h/c`)
- Cryptographically secure random number generation
- Cross-platform support (Windows, macOS, Linux)
- Can be used as custom entropy source for our random pool:
```c
// Use fabric's secure random as entropy source
random_pool_set_entropy_source(fabric_secure_random_bytes);
```

### 2. Thread Safety (`fabric/src/threads.h/c`)
- Thread-safe wrappers for mutexes, rwlocks, condition variables
- Atomic operations with safety checks
- Could replace pthread calls in random_pool.c for better error handling

### 3. WebGPU Integration (`fabric/src/webgpu/`)
- Dawn integration code
- Shader sources for crypto operations
- Can be combined with our WebGPU renderer

## Integration Approach

### Option 1: Direct Integration
Link fabric as a library:
```makefile
FABRIC_DIR = ~/fabric/src
INCLUDES += -I$(FABRIC_DIR)
SOURCES += $(FABRIC_DIR)/secure_random.c $(FABRIC_DIR)/threads.c
```

### Option 2: Selective Import
Copy only needed components to avoid conflicts:
- Import `secure_random.*` for better entropy
- Import `threads.*` for enhanced thread safety
- Import WebGPU shaders for GPU random generation

### Option 3: Shared Library
Build fabric as a shared library:
```bash
cd ~/fabric/src
gcc -shared -fPIC -o libfabric.so *.c
```

## Potential Conflicts

1. **Error Codes**: Both projects define error types
   - Solution: Namespace fabric errors as `FABRIC_*`

2. **Thread Primitives**: Fabric wraps pthread differently
   - Solution: Use fabric's wrappers for consistency

3. **WebGPU Headers**: Dawn headers may conflict
   - Solution: Use fabric's dawn headers consistently

## Recommended Integration

1. **Phase 1**: Use fabric's secure_random as entropy source
   ```c
   // In random_pool.c
   #include "fabric/secure_random.h"

   static int fabric_entropy_wrapper(uint8_t* buffer, size_t length) {
       return fabric_secure_random_bytes(buffer, length) == FABRIC_SUCCESS ? 0 : -1;
   }

   // During init
   random_pool_set_entropy_source(fabric_entropy_wrapper);
   ```

2. **Phase 2**: Integrate WebGPU components
   - Use fabric's shader loading
   - Share Dawn context between renderers
   - Implement GPU-accelerated random generation

3. **Phase 3**: Unify thread safety
   - Replace raw pthread with fabric wrappers
   - Add atomic operations for lock-free paths
   - Implement double-buffering with fabric primitives

## WebGPU Random Generation

Fabric includes shaders that could generate random numbers on GPU:
```wgsl
// Adapted from fabric's crypto shaders
fn gpu_random(seed: u32, index: u32) -> u32 {
    var x = seed + index;
    x = ((x >> 16u) ^ x) * 0x45d9f3bu;
    x = ((x >> 16u) ^ x) * 0x45d9f3bu;
    x = (x >> 16u) ^ x;
    return x;
}
```

This could fill buffers much faster than CPU generation.

## Next Steps

1. Test fabric's secure_random performance vs SecRandomCopyBytes
2. Benchmark GPU random generation feasibility
3. Create unified error handling between projects
4. Document API compatibility layer
