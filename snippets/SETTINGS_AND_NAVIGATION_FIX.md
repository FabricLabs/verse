# Settings Persistence and Navigation Fixes

## Issues Addressed

### 1. ✅ **Settings Not Loading at Startup** - FIXED

**Problem**: Changing music volume to 0%, exiting, and relaunching caused music to start playing again.

**Root Cause**: Settings were being loaded into `window_state` but not applied to the `background_music` system.

**Solution**: Added code to apply loaded settings to the background music system after initialization:

```c
// In src/verse_client.c - after background_music_start()
background_music_set_music_volume(g_background_music, window_state.music_volume / 100.0f);
background_music_set_enabled(g_background_music, window_state.background_music_enabled);
```

**Verification**: Debug logs now show:
```
Loaded settings: music_volume=0%, background_music_enabled=true
Applied music volume: 0% (0.00), music enabled: true
Background music volume: 0.00
```

### 2. ❌ **Arrow Keys Not Working on Main Menu** - Investigating

**Problem**: Arrow keys not selecting buttons in the menu (user has to click).

**Initial Finding**: User was testing arrow keys on the **title screen** (screen 0), not the main menu (screen 1).
- Title screen only responds to ENTER/SPACE to proceed
- Arrow keys are designed to work on screens with multiple buttons

**Testing Required**: Need to verify arrow keys work on actual main menu with multiple buttons.

## Technical Details

### Settings Loading Flow (Fixed)
1. `window_init()` calls `settings_load()` from `settings.dat`
2. `settings_apply_to_window_state()` applies to `window_state`
3. `window_set_scale_factor()` applies window scaling
4. **NEW**: Background music system initialized
5. **NEW**: `background_music_set_music_volume()` and `background_music_set_enabled()` apply settings

### Navigation Flow (Investigating)
1. `SDL_KEYDOWN` event with `SDLK_UP`/`SDLK_DOWN`
2. `handle_key_press()` checks `current_screen`
3. For main menu: calls `window_next_selection()` / `window_prev_selection()`
4. Functions update `window_state.buttons[]` selection state
5. Visual highlighting updated on next render

## Test Sequence

### ✅ **Settings Test (Working)**
1. Launch game
2. Go to Settings → Music Volume → Change to 0%
3. Exit game
4. Relaunch game
5. **Expected**: No music plays
6. **Result**: ✅ Working - music volume correctly loaded as 0%

### 🔍 **Navigation Test (Need to Test on Main Menu)**
1. Launch game
2. **Press ENTER on title screen** → Reach main menu
3. Try arrow keys on main menu with buttons: "New Game", "Load Game", "Settings", "Exit"
4. **Expected**: Selection should move between buttons
5. **Result**: Need to test this specific workflow

## Files Modified

### `src/verse_client.c`
- Added settings application to background music system after initialization
- Added debug output for arrow key presses including screen number

### `src/window.c`
- Added debug output for settings loading values
- Re-added debug output to selection functions

## Remaining Work

1. **Test arrow keys on actual main menu** (not title screen)
2. **Remove debug output** once confirmed working
3. **Update documentation** with findings

## Current Status

- ✅ **Settings persistence**: FIXED and verified working
- 🔍 **Arrow key navigation**: Testing in progress (likely working, just wrong screen tested)
