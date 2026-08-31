#!/bin/bash
# Simple compile script for minimal mob solver test

echo "Compiling simple mob solver test..."
gcc -Wall -Wextra -std=c99 -O2 -g -o test_mob_solver_simple test_mob_solver_simple.c mob_ai.c -lm

if [ $? -eq 0 ]; then
    echo "Compilation successful!"
    echo "Run with: ./test_mob_solver_simple"
else
    echo "Compilation failed!"
fi
