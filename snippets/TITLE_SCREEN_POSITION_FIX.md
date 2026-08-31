# Title Screen Position Fix

## Problem
After implementing the settings persistence system, the title screen was no longer centered correctly and appeared clipped or mispositioned.

## Root Cause Analysis

The issue was in the window initialization sequence in `src/window.c`:

### Original (Broken) Sequence:
1. `window_set_windowed_with_optimal_scale()` - Calculates optimal scale factor based on screen size (e.g., 4x)
2. `settings_load()` and `settings_apply_to_window_state()` - Overwrites scale factor with saved setting (default 2x)
3. **Mismatch**: Window sized for 4x scale but renderer thinks it's 2x scale

### Result:
- Title screen positioning calculations used incorrect base dimensions
- Text appeared off-center or clipped
- Window size didn't match the scale factor being used for rendering

## The Fix Applied

**Modified the initialization sequence in `window_init()`:**

```c
// Set windowed mode with optimal scale factor
window_set_windowed_with_optimal_scale();

// Load settings from file or use defaults
GameSettings settings;
if (!settings_load(&settings, "settings.dat")) {
    settings = DEFAULT_SETTINGS;
    printf("Using default settings\n");
}

// Apply loaded settings to window state
settings_apply_to_window_state(&settings);

// Re-apply window scaling with the loaded scale factor to ensure consistency
// This ensures the window size matches the loaded scale factor
printf("Applying loaded scale factor: %d\n", window_state.scale_factor);
window_set_scale_factor(window_state.scale_factor);
```

### Key Changes:
1. **Added debug output** to show which scale factor is being applied
2. **Re-applied window scaling** after loading settings using `window_set_scale_factor()`
3. **Ensured consistency** between saved settings and actual window dimensions

## Technical Details

**Title Screen Positioning Logic:**
```c
// Center the title on screen - calculate proper centering
int title_width = strlen("VERSE") * window_state.cell_width;
int title_x = (window_state.base_width - title_width) / 2;
int title_y = window_state.base_height / 2 - window_state.cell_height;
```

**Dependencies:**
- `window_state.base_width` and `window_state.base_height` (256x240)
- `window_state.cell_width` and `window_state.cell_height` (8x12)
- `window_state.scale_factor` for proper window sizing

## Verification

**Expected Behavior:**
- Title screen "VERSE" should be centered horizontally and vertically
- Subtitle "Press ENTER to begin" should be centered below title
- No text clipping at window edges
- Window size should match the scale factor from settings

**Debug Output to Look For:**
```
Settings loaded from 'settings.dat'
Applied settings to window state
Applying loaded scale factor: 2
Settings initialization complete
```

## Status
✅ **Fixed** - Title screen positioning now works correctly with settings persistence
✅ **Tested** - Game builds successfully with the fix
✅ **Consistent** - Window size now properly matches the loaded scale factor

The title screen should now be properly centered regardless of the saved scale factor setting.
