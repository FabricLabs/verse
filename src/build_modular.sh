#!/bin/bash
# Build script for modular verse implementations
# This keeps original files untouched and creates separate executables

set -e  # Exit on error

echo "🔨 Building Modular Verse Client..."
echo "=================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if original verse_client exists
if [ -f "verse_client" ]; then
    echo -e "${GREEN}✓ Original verse_client found${NC}"
else
    echo -e "${YELLOW}⚠ Original verse_client not found - that's OK${NC}"
fi

# Clean previous modular build
echo "Cleaning previous modular build..."
make -f Makefile.modular clean >/dev/null 2>&1 || true

# Build modular version
echo "Building modular client..."
if make -f Makefile.modular verse_client_modular; then
    echo -e "${GREEN}✅ Build successful!${NC}"
    echo ""
    echo "Executables created:"
    echo "  - verse_client_modular (new modular version)"
    if [ -f "verse_client" ]; then
        echo "  - verse_client (original - unchanged)"
    fi
    echo ""
    echo "To run the modular version:"
    echo "  ./verse_client_modular"
    echo ""
    echo "To run the original version:"
    echo "  ./verse_client"
else
    echo -e "${RED}❌ Build failed${NC}"
    exit 1
fi

# Optional: Build and run tests
read -p "Build and run module tests? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Building world module test..."
    if gcc -I. -Inoise-c/include -o test_world_modular test_world_modular.c \
        world_core.c world_voxel.c world_generation_dispatch.c \
        world_generation_simple.c world_serialize.c world_noise.c \
        noise-c/src/crypto/sha2/sha256.c -lm; then
        echo -e "${GREEN}✓ World module test built${NC}"
        echo "Running test..."
        ./test_world_modular
    fi

    echo ""
    echo "Building random pool test..."
    if gcc -I. -o test_random_pool test_random_pool.c random_pool.c \
        world_random_pool.c -lpthread -lm -framework Security; then
        echo -e "${GREEN}✓ Random pool test built${NC}"
        echo "Run with: ./test_random_pool"
    fi
fi

echo ""
echo -e "${GREEN}🎉 Modular build complete!${NC}"
