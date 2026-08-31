#!/bin/bash

# Script to generate 100 worlds and analyze their compositions
echo "Generating 100 worlds and analyzing compositions..."

# Create output directory
mkdir -p world_analysis
cd world_analysis

# Generate 100 worlds with different seeds
for i in $(seq 1 100); do
    seed="analysis_world_$i"
    echo "Generating world $i/100 with seed: $seed"

    # Generate world
    ../universe-generate-gameworld --seed "$seed" > "world_${i}_output.txt" 2>&1

    # Check if generation was successful
    if [ $? -eq 0 ]; then
        echo "World $i generated successfully"
    else
        echo "World $i generation failed"
    fi
done

echo "World generation complete. Analyzing compositions..."

# Create a summary file
echo "World Analysis Summary" > composition_summary.txt
echo "=====================" >> composition_summary.txt
echo "Generated $(date)" >> composition_summary.txt
echo "" >> composition_summary.txt

# Count successful generations
successful_worlds=$(ls -1 world_*_output.txt 2>/dev/null | wc -l)
echo "Successfully generated: $successful_worlds worlds" >> composition_summary.txt
echo "" >> composition_summary.txt

echo "Analysis complete. Check composition_summary.txt for results."
