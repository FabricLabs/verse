# Toolbar Migration Guide

This guide shows how to migrate the toolbar from the old scattered UI system to the new unified UI system in `world_editor.c`.

## What We're Migrating

### Old System (Current)
```c
// Scattered global variables
static SDL_Rect g_btn_cursor_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_shape_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_cube_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_sphere_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_adjacent_rect = {0, 0, 0, 0};

// Manual rendering (50+ lines of code)
static void draw_tools_panel(SDL_Renderer *ren, TTF_Font *font, ...) {
    // Manual button rendering for each tool
    g_btn_cursor_rect = (SDL_Rect){ctrl_bg.x + 8, controls_y, 70, controls_h};
    SDL_SetRenderDrawColor(ren, g_current_tool == TOOL_CURSOR ? 90 : 50, ...);
    SDL_RenderFillRect(ren, &g_btn_cursor_rect);
    SDL_RenderDrawRect(ren, &g_btn_cursor_rect);
    draw_text(ren, font, "Select", g_btn_cursor_rect.x + 8, ...);
    // ... repeat for each button
}

// Manual click handling (20+ lines of coordinate checking)
if (mx >= g_btn_cursor_rect.x && mx < g_btn_cursor_rect.x + g_btn_cursor_rect.w &&
    my >= g_btn_cursor_rect.y && my < g_btn_cursor_rect.y + g_btn_cursor_rect.h) {
    g_current_tool = TOOL_CURSOR;
    continue;
}
// ... repeat for each button
```

### New System (Migrated)
```c
// Single UI system pointer
static WorldEditorUI* g_editor_ui = NULL;

// Clean initialization
g_editor_ui = world_editor_ui_create(renderer, NULL);
world_editor_ui_create_toolbar(g_editor_ui, 10, 10);

// Automatic rendering
world_editor_ui_render(g_editor_ui);

// Automatic event handling
if (world_editor_ui_handle_event(g_editor_ui, &event)) {
    continue; // UI handled the event
}
```

## Migration Steps

### Step 1: Add New Files to Project

1. **Copy the new UI system files**:
   ```bash
   cp src/ui_system.h src/
   cp src/ui_system.c src/
   cp src/world_editor_ui.h src/
   cp src/world_editor_ui.c src/
   ```

2. **Update your main Makefile** to include the new sources:
   ```makefile
   # Add to your existing Makefile
   UI_SOURCES = src/ui_system.c src/world_editor_ui.c
   UI_OBJECTS = $(UI_SOURCES:.c=.o)

   # Include in your main target
   world-editor: $(UI_OBJECTS) $(OTHER_OBJECTS)
       $(CC) $(UI_OBJECTS) $(OTHER_OBJECTS) -o $@ $(LDFLAGS)
   ```

### Step 2: Update world_editor.c Includes

Add the new header at the top of `world_editor.c`:
```c
// Add this include
#include "world_editor_ui.h"
```

### Step 3: Replace Global Variables

**Remove these old global variables**:
```c
// REMOVE these lines
static SDL_Rect g_btn_cursor_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_shape_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_cube_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_sphere_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_adjacent_rect = {0, 0, 0, 0};
```

**Add this new global variable**:
```c
// ADD this line
static WorldEditorUI* g_editor_ui = NULL;
```

### Step 4: Replace Initialization

**Find the initialization code** (usually in `main()` or an init function) and **replace**:
```c
// OLD initialization code (remove this)
// (scattered throughout the code)

// NEW initialization code (add this)
g_editor_ui = world_editor_ui_create(renderer, NULL);
if (!g_editor_ui) {
    printf("Failed to create world editor UI\n");
    return 1;
}

// Set up callbacks
world_editor_ui_set_tool_callback(g_editor_ui, on_tool_change, NULL);

// Create UI panels
world_editor_ui_create_toolbar(g_editor_ui, 10, 10);
```

### Step 5: Add Callback Function

**Add this callback function** to handle tool changes:
```c
static void on_tool_change(int tool_id, void* user_data) {
    (void)user_data; // Unused parameter

    // Convert new tool IDs to old enum values
    switch (tool_id) {
        case WORLD_EDITOR_TOOL_SELECT:
            g_current_tool = TOOL_CURSOR;
            break;
        case WORLD_EDITOR_TOOL_DRAW:
            g_current_tool = TOOL_SHAPE;
            break;
        case WORLD_EDITOR_TOOL_CUBE:
            g_shape_mode = SHAPE_CUBE;
            break;
        case WORLD_EDITOR_TOOL_SPHERE:
            g_shape_mode = SHAPE_SPHERE;
            break;
        case WORLD_EDITOR_TOOL_ADJACENT:
            // Handle adjacent tool
            break;
        case WORLD_EDITOR_TOOL_FILL:
            // Handle fill tool
            break;
        case WORLD_EDITOR_TOOL_ERASE:
            // Handle erase tool
            break;
    }

    printf("Tool changed to: %d\n", tool_id);
}
```

### Step 6: Replace Rendering

**Find the `draw_tools_panel` function** and **replace the entire function** with:
```c
static void draw_tools_panel(SDL_Renderer *ren, TTF_Font *font, ...) {
    // OLD: Remove all the manual button rendering code

    // NEW: Just render the UI system
    if (g_editor_ui) {
        world_editor_ui_render(g_editor_ui);
    }
}
```

**Or, if you want to keep the old function for now**, just add the new rendering:
```c
static void draw_tools_panel(SDL_Renderer *ren, TTF_Font *font, ...) {
    // Keep existing code for now...

    // Add new UI rendering
    if (g_editor_ui) {
        world_editor_ui_render(g_editor_ui);
    }
}
```

### Step 7: Replace Event Handling

**Find the mouse click handling code** and **replace**:
```c
// OLD: Remove all the manual coordinate checking
if (mx >= g_btn_cursor_rect.x && mx < g_btn_cursor_rect.x + g_btn_cursor_rect.w &&
    my >= g_btn_cursor_rect.y && my < g_btn_cursor_rect.y + g_btn_cursor_rect.h) {
    g_current_tool = TOOL_CURSOR;
    continue;
}
// ... remove all other button checks

// NEW: Add this before your existing event handling
if (world_editor_ui_handle_event(g_editor_ui, &event)) {
    // Event was handled by UI system
    continue;
}
```

### Step 8: Update Cleanup

**Find the cleanup code** and **add**:
```c
// Add this to your cleanup function
if (g_editor_ui) {
    world_editor_ui_destroy(g_editor_ui);
    g_editor_ui = NULL;
}
```

## Testing the Migration

### Build and Test
1. **Build the project**:
   ```bash
   make clean
   make
   ```

2. **Run the world editor** and verify:
   - Toolbar appears correctly
   - Buttons respond to mouse clicks
   - Tool selection works
   - No crashes or errors

### Debugging
If you encounter issues:
1. **Check console output** for error messages
2. **Verify SDL2 and SDL2_ttf** are properly linked
3. **Check that all new files** are included in the build
4. **Ensure the renderer** is passed correctly to the UI system

## Gradual Migration Strategy

### Phase 1: Add New System (Current)
- ✅ Add new UI system files
- ✅ Keep existing code working
- ✅ Test that new system compiles

### Phase 2: Replace Toolbar (Current)
- ✅ Replace toolbar rendering
- ✅ Replace toolbar event handling
- ✅ Test toolbar functionality

### Phase 3: Remove Old Code
- ❌ Remove old toolbar variables
- ❌ Remove old toolbar rendering code
- ❌ Remove old toolbar event handling
- ❌ Clean up unused code

### Phase 4: Migrate Other Panels
- ❌ Migrate palette
- ❌ Migrate file operations
- ❌ Migrate world info
- ❌ Migrate actor management
- ❌ Migrate settings

## Benefits of This Migration

1. **Code Reduction**: From 50+ lines to 1 function call
2. **Maintainability**: Changes happen in one place
3. **Consistency**: All UI elements use the same styling
4. **Reusability**: Can be used in other parts of VERSE
5. **Extensibility**: Easy to add new tools and features

## Next Steps

After successfully migrating the toolbar:
1. **Test thoroughly** to ensure no regressions
2. **Migrate the next panel** (suggest palette next)
3. **Continue the gradual migration** process
4. **Add new features** to the toolbar using the new system

The toolbar migration provides a solid foundation for migrating the rest of the UI system!
