#!/bin/bash

# Test script for world generation benchmark
echo "Building world generation benchmark..."
make world-generation-benchmark

if [ $? -eq 0 ]; then
    echo "Build successful! Running benchmark..."
    echo ""

    # Run basic benchmark
    echo "=== Basic Benchmark ==="
    ./world-generation-benchmark --iterations 3 --size 32

    echo ""
    echo "=== CSV Output Test ==="
    ./world-generation-benchmark --iterations 2 --size 32 --csv

    echo ""
    echo "=== Single Type Test ==="
    ./world-generation-benchmark --iterations 2 --size 32 --type WILDERNESS

    echo ""
    echo "=== Help Test ==="
    ./world-generation-benchmark --help

else
    echo "Build failed!"
    exit 1
fi
