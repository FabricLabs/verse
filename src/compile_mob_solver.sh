#!/bin/bash
# Compile script for mob solver test

echo "Compiling mob solver test..."

# Compiler settings
CC="gcc"
CFLAGS="-Wall -Wextra -std=c99 -O2 -g -I. -Inoise-c/include"
LDFLAGS="-lm"

# Source files
SOURCES="test_mob_solver.c \
         mob_ai.c \
         world.c \
         world_core.c \
         world_voxel.c \
         world_physics.c \
         world_generation_dispatch.c \
         world_generation_labyrinth.c \
         world_generation_simple.c \
         world_generation_wilderness.c \
         world_generation_scoured.c \
         world_generation_farm.c \
         world_noise.c \
         world_bulk_ops.c \
         world_step_actors_extended.c \
         actor.c \
         constants.c \
         entropy_field.c \
         universe.c \
         character.c \
         noise-c/src/crypto/sha2/sha256.c"

# Output executable
OUTPUT="test_mob_solver"

# Compile
$CC $CFLAGS $SOURCES -o $OUTPUT $LDFLAGS

if [ $? -eq 0 ]; then
    echo "Compilation successful!"
    echo "Run with: ./$OUTPUT [options]"
    echo "Options:"
    echo "  --seed <seed>        World generation seed"
    echo "  --max-steps <n>      Maximum simulation steps"
    echo "  --step-time <t>      Time per simulation step"
    echo "  --verbose            Print detailed progress"
else
    echo "Compilation failed!"
    exit 1
fi
