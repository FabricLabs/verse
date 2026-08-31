# Starfighter - VERSE Space Combat Game

A space combat game that leverages existing VERSE engine methods and structs to present a new game experience where the player controls a spaceship in empty space.

## Features

- **3D Space Environment**: OpenGL-rendered 3D space with procedurally generated star field
- **Player Starship**: Simple flattened pyramid (polygon) design rendered in 3D
- **Mouse Control**: Mouse is locked to a reticle at the center of the screen for precise aiming
- **Keyboard Movement**: WASD controls for ship movement, Space/Shift for vertical movement
- **Dynamic Camera**: Third-person camera that follows the ship
- **HUD Elements**: Health and energy bars, crosshair reticle
- **Menu System**: ESC key toggles mouse lock and opens menu

## Controls

- **W/A/S/D**: Move ship forward/left/backward/right
- **Space**: Move ship up
- **Left Shift**: Move ship down
- **Mouse**: Look around (when locked)
- **ESC**: Toggle mouse lock/menu
- **ESC again**: Exit game

## Technical Details

### Rendering
- Uses OpenGL 2.1 for 3D graphics
- Custom perspective and look-at matrix functions (compatible with modern OpenGL)
- Star field with 1000 procedurally generated stars
- Ship rendered as a flattened pyramid with engine glow effects

### Architecture
- Built on top of existing VERSE engine infrastructure
- Leverages existing constants, world structures, and utility functions
- Standalone program that can be built independently
- Uses SDL2 for window management and input handling

### Game State
- Player ship with position, orientation, velocity, health, and energy
- Dynamic star field that moves relative to player position
- Camera system that follows the ship
- Input state management for mouse lock and menu system

## Building

The starfighter program is integrated into the main VERSE Makefile:

```bash
# Build just the starfighter program
make starfighter

# Build all programs including starfighter
make all

# Clean build artifacts
make clean
```

## Dependencies

- SDL2 (for window management and input)
- OpenGL (for 3D rendering)
- Standard C libraries (math, stdio, stdlib, etc.)

## Integration with VERSE Engine

The starfighter program demonstrates how to leverage existing VERSE engine components:

- **Constants**: Uses `constants.h` for game constants
- **World Structures**: Leverages world and voxel concepts
- **Rendering Patterns**: Follows similar patterns to other VERSE renderers
- **Build System**: Integrated into the main Makefile

## Future Enhancements

Potential areas for expansion:

- Enemy ships and combat mechanics
- Power-ups and weapon systems
- Mission objectives and scoring
- Sound effects and background music
- Particle effects for explosions and engine trails
- Multiplayer support
- More complex ship models and animations

## Troubleshooting

If the game exits immediately:
- Check that OpenGL is properly supported on your system
- Ensure SDL2 is correctly installed
- Verify that the window is visible and not minimized
- Check console output for any error messages

## License

This program is part of the VERSE engine project and follows the same licensing terms.
