# Verse Client Decomposition Design

## Overview

verse_client.c is a 1,124-line file that serves as the main game client. It's much smaller and better organized than world.c, but can still benefit from modular decomposition.

## Current Structure Analysis

### Major Components:

1. **Audio Systems** (Lines 1-70)
   - Background music integration
   - UI sounds
   - Title hum system
   - Sound effect functions

2. **Input Handling** (Lines 81-720)
   - Keyboard input (`handle_key_press`)
   - Button clicks (`handle_button_click`)
   - Mouse input (`handle_mouse_click`, `handle_mouse_motion`)
   - Text input for player name

3. **Rendering** (Lines 741-852)
   - Main render function (`render_game`)
   - Screen state management
   - Title screen handling
   - Modal dialogs (exit prompt, name input)

4. **Game Loop** (Lines 857-1009)
   - Main game loop with timing
   - FPS calculation
   - Event handling
   - State updates
   - Music updates

5. **Main/Initialization** (Lines 1011-1124)
   - SDL initialization
   - System setup
   - Resource loading
   - Cleanup

## Proposed Modular Architecture

### 1. **client_audio.h/c** - Audio Management
```c
// All audio system integration
- Background music system
- UI sounds
- Title hum
- Sound effect playback functions
- Audio initialization/cleanup
```

### 2. **client_input.h/c** - Input Handling
```c
// All input processing
- Keyboard handling
- Mouse handling (click, motion, wheel)
- Button click processing
- Text input management
- Input state tracking
```

### 3. **client_render.h/c** - Rendering Pipeline
```c
// Screen rendering logic
- Screen state rendering
- Title screen
- Game screen selection
- Modal dialog rendering
- Frame presentation
```

### 4. **client_game_loop.h/c** - Main Game Loop
```c
// Core game loop functionality
- Frame timing
- Delta time calculation
- FPS tracking
- Event processing
- State updates
- Performance monitoring
```

### 5. **client_screens.h/c** - Screen Management
```c
// Individual screen logic
- Main menu screen
- Settings screen
- Loading screen
- Chapter/story screen
- In-game menu
- Screen transitions
```

### 6. **client_init.h/c** - Initialization/Cleanup
```c
// System initialization
- SDL setup
- Window creation
- Audio system init
- Resource loading
- Cleanup routines
```

### 7. **client_state.h/c** - Client State Management
```c
// Global state coordination
- Game state access
- Screen state management
- Title screen state
- Exit handling
- State synchronization
```

## Implementation Plan

### Phase 1: Create Headers
1. Define interfaces for each module
2. Establish clear boundaries
3. Document responsibilities

### Phase 2: Extract Functions
1. Start with self-contained modules (audio, init)
2. Move to interconnected modules (input, render)
3. Handle dependencies carefully

### Phase 3: Update Main
1. Reduce verse_client.c to just main()
2. Include all module headers
3. Call initialization from modules

### Phase 4: Test & Validate
1. Ensure all functionality preserved
2. Test each screen/mode
3. Verify performance unchanged

## Benefits

1. **Maintainability**: Easier to find and modify specific functionality
2. **Testability**: Can unit test individual modules
3. **Reusability**: Modules can be reused in other clients
4. **Clarity**: Clear separation of concerns
5. **Compilation**: Faster incremental builds

## Challenges

1. **Global State**: Need to manage g_game_state access
2. **Callbacks**: Window system expects certain globals
3. **Dependencies**: Some circular dependencies to resolve
4. **SDL Events**: Need central event routing

## Module Dependencies

```
client_init
    ├── client_audio
    ├── client_state
    └── window

client_game_loop
    ├── client_input
    ├── client_render
    ├── client_audio
    └── client_state

client_input
    ├── client_state
    └── window

client_render
    ├── client_screens
    ├── client_state
    └── window

client_screens
    ├── client_state
    └── window
```

## Next Steps

1. Create module headers with clear interfaces
2. Start with client_audio.c extraction
3. Move to client_init.c
4. Continue with other modules
5. Update build system
