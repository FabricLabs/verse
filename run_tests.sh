#!/bin/bash

# Exit on any error
set -e

echo "Building all tests..."
make condition_test
make engine_test

echo -e "\n========================================"
echo "Running condition_test..."
echo "========================================"
./condition_test

echo -e "\n========================================"
echo "Running engine_test..."
echo "========================================"
./engine_test

echo -e "\n========================================"
echo "All tests completed successfully!"
echo "========================================" 