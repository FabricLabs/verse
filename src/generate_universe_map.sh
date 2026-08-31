#!/bin/bash

# Universe Map Generator Script
# This script builds and runs the universe map generator

set -e  # Exit on any error

echo "=== Universe Map Generator ==="
echo "Building universe map generator..."

# Check if SDL2 is available
if ! pkg-config --exists sdl2; then
    echo "Error: SDL2 not found. Please install SDL2:"
    echo "  macOS: brew install sdl2"
    echo "  Ubuntu: sudo apt-get install libsdl2-dev"
    echo "  CentOS: sudo yum install SDL2-devel"
    exit 1
fi

# Create assets directory
mkdir -p assets

# Build the generator
make -f Makefile.universe_map clean
make -f Makefile.universe_map

echo "Running universe map generator..."
echo "This may take several minutes to generate 32x32 wilderness worlds..."

# Run the generator
./universe_map_generator

echo "=== Generation Complete ==="
echo "Output: assets/universe_map.bmp"
echo ""

# Check if the file was created
if [ -f "assets/universe_map.bmp" ]; then
    # Get file size
    SIZE=$(ls -lh assets/universe_map.bmp | awk '{print $5}')
    echo "✓ Universe map generated successfully!"
    echo "  File: assets/universe_map.bmp"
    echo "  Size: $SIZE"

    # Try to get image dimensions if possible
    if command -v identify >/dev/null 2>&1; then
        DIMS=$(identify -format "%wx%h" assets/universe_map.bmp 2>/dev/null || echo "unknown")
        echo "  Dimensions: $DIMS"
    fi
else
    echo "✗ Error: universe_map.bmp was not created"
    exit 1
fi

echo ""
echo "You can now view the universe map with any image viewer!"
