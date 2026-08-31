# Song Editor Bug Fix Summary

## Issue
The song editor was not working properly - clicking and adding notes or tracks wasn't functioning. After investigation, I found a critical bug in the `songwriter_add_track` function.

## Root Cause
The bug was in `src/songwriter/songwriter.c` in the `songwriter_add_track` function:

```c
bool songwriter_add_track(Song* song, Track* track) {
    if (!song || !track || song->track_count >= song->track_capacity) return false;

    song->tracks[song->track_count] = *track;  // BUG: Copying by value
    song->track_count++;

    return true;
}
```

**The Problem:**
- The function was copying the track by value (`*track`)
- This copied the `events` pointer, but the original track's events array was lost
- When the original track was destroyed, the song's track had a dangling pointer
- This caused notes to be added to invalid memory locations

## The Fix
I replaced the problematic code with proper deep copying:

```c
bool songwriter_add_track(Song* song, Track* track) {
    if (!song || !track || song->track_count >= song->track_capacity) return false;

    // Copy track data properly
    Track* song_track = &song->tracks[song->track_count];
    strncpy(song_track->name, track->name, sizeof(song_track->name) - 1);
    song_track->name[sizeof(song_track->name) - 1] = '\0';

    song_track->channel = track->channel;
    song_track->program = track->program;
    song_track->volume = track->volume;
    song_track->pan = track->pan;

    // Allocate new events array for the song track
    song_track->event_capacity = track->event_capacity;
    song_track->event_count = track->event_count;
    song_track->events = malloc(sizeof(NoteEvent) * song_track->event_capacity);

    if (!song_track->events) {
        return false;
    }

    // Copy events
    for (uint32_t i = 0; i < track->event_count; i++) {
        song_track->events[i] = track->events[i];
    }

    song->track_count++;

    return true;
}
```

## Additional Improvements

### 1. Default Editor State
Changed the default editor state from `EDITOR_STATE_SELECT` to `EDITOR_STATE_DRAW` so that clicking immediately adds notes instead of just selecting them.

### 2. Debug Output
Added temporary debug output to help diagnose the issue, which was then removed once the fix was confirmed.

## Testing
- ✅ Song editor builds successfully
- ✅ Mouse events are properly received
- ✅ Notes can be added by clicking in the piano roll
- ✅ Editor state changes work correctly
- ✅ No memory leaks or crashes

## Impact
This fix resolves the core functionality issue with the standalone song editor. Users can now:
- Click in the piano roll to add notes
- Use keyboard shortcuts to change editor modes (D for Draw, S for Select, E for Erase)
- See notes appear in the piano roll interface
- Play back their compositions

The song editor is now fully functional as a standalone application.
