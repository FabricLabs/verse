#!/bin/bash
# Test compilation of modular client

echo "Testing modular client compilation..."

# Client modules
CLIENT_MODULES="client_init.c client_state.c client_audio.c client_input.c client_render.c client_game_loop.c"

# Required dependencies
DEPS="window.c game_state.c world.c isometric_renderer.c background_music.c ui_sounds.c title_hum.c"
DEPS="$DEPS actor.c character.c constants.c entropy_field.c fluid_navier_stokes.c"
DEPS="$DEPS fp_renderer.c gpu_physics.c gpu_voxel_buffer.c greedy_mesh.c input_manager.c"
DEPS="$DEPS octree.c player.c settings.c unified_renderer.c universe.c universe_coords.c"
DEPS="$DEPS voxel_mesh.c world_bulk_ops.c world_editor_input.c world_editor_ui.c"
DEPS="$DEPS world_entropy_generator.c world_spawn.c world_transition.c"
DEPS="$DEPS songwriter/songwriter.c songwriter/instrument.c songwriter/pattern.c songwriter/scale.c"

# SDL flags
SDL_FLAGS=$(sdl2-config --cflags --libs)

# Compile command
echo "Compiling modular client..."
gcc -o verse_client_modular verse_client_modular.c $CLIENT_MODULES $DEPS \
    -I. -Isongwriter -Inoise-c/include \
    $SDL_FLAGS -lm -framework OpenGL

if [ $? -eq 0 ]; then
    echo "✅ Modular client compiled successfully!"
    echo "Binary created: verse_client_modular"
else
    echo "❌ Compilation failed"
    exit 1
fi
