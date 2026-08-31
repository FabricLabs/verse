#!/bin/bash
# Simple build script that handles SDL2 include path issues

echo "🔨 Building Modular Verse Client (Simple Version)..."
echo "=================================================="

# Get SDL2 flags
SDL_CFLAGS=$(sdl2-config --cflags)
SDL_LIBS=$(sdl2-config --libs)

# Base flags
CFLAGS="-Wall -g -I. -Isongwriter -Inoise-c/include"
LIBS="-lm -framework OpenGL"

# Client modules
CLIENT_MODULES="client_init.c client_state.c client_audio.c client_input.c client_render.c client_game_loop.c verse_client_modular.c"

# Essential dependencies only (for testing)
DEPS="window.c game_state.c world.c isometric_renderer.c"
DEPS="$DEPS background_music.c ui_sounds.c title_hum.c"
DEPS="$DEPS songwriter/songwriter.c songwriter/instrument.c songwriter/pattern.c songwriter/scale.c"

# Add more dependencies as stubs to get it to link
STUBS="actor.c character.c constants.c entropy_field.c"
STUBS="$STUBS fluid_navier_stokes.c fp_renderer.c gpu_physics.c gpu_voxel_buffer.c"
STUBS="$STUBS greedy_mesh.c input_manager.c octree.c player.c settings.c"
STUBS="$STUBS unified_renderer.c universe.c universe_coords.c voxel_mesh.c"
STUBS="$STUBS world_bulk_ops.c world_editor_input.c world_editor_ui.c"
STUBS="$STUBS world_entropy_generator.c world_spawn.c world_transition.c wfc.c"

# Compile with adjusted include path
echo "Compiling modular client..."
gcc -o verse_client_modular_test \
    $CLIENT_MODULES $DEPS $STUBS \
    $CFLAGS $SDL_CFLAGS \
    $SDL_LIBS $LIBS \
    2>&1 | head -20

if [ ${PIPESTATUS[0]} -eq 0 ]; then
    echo "✅ Build successful! Created: verse_client_modular_test"
    echo ""
    echo "Note: This test build may have limited functionality."
    echo "For full build, use the main Makefile after fixing SDL2 includes."
else
    echo "❌ Build failed. SDL2 include path issue persists."
    echo ""
    echo "To fix permanently, edit game_state.h:"
    echo "  Change: #include <SDL2/SDL.h>"
    echo "  To:     #include <SDL.h>"
fi
