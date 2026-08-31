#!/bin/bash
# Compare original vs modular implementations

echo "📊 Verse Implementation Comparison"
echo "=================================="
echo ""

# Function to count lines
count_lines() {
    if [ -f "$1" ]; then
        wc -l "$1" | awk '{print $1}'
    else
        echo "0"
    fi
}

# Original implementation
echo "ORIGINAL IMPLEMENTATION:"
echo "----------------------"
ORIG_LINES=$(count_lines "verse_client.c")
echo "  verse_client.c: $ORIG_LINES lines"
if [ -f "verse_client" ]; then
    SIZE=$(ls -lh verse_client | awk '{print $5}')
    echo "  Executable: verse_client ($SIZE)"
else
    echo "  Executable: not built"
fi

echo ""
echo "MODULAR IMPLEMENTATION:"
echo "----------------------"

# Client modules
TOTAL_LINES=0
for module in client_init client_state client_audio client_input client_render client_game_loop; do
    H_LINES=$(count_lines "${module}.h")
    C_LINES=$(count_lines "${module}.c")
    MODULE_LINES=$((H_LINES + C_LINES))
    TOTAL_LINES=$((TOTAL_LINES + MODULE_LINES))
    printf "  %-20s %4d lines (.h: %3d, .c: %4d)\n" "$module:" "$MODULE_LINES" "$H_LINES" "$C_LINES"
done

# Main file
MAIN_LINES=$(count_lines "verse_client_modular.c")
TOTAL_LINES=$((TOTAL_LINES + MAIN_LINES))
printf "  %-20s %4d lines\n" "verse_client_modular:" "$MAIN_LINES"

echo "  ----------------------"
echo "  Total:              $TOTAL_LINES lines"

if [ -f "verse_client_modular" ]; then
    SIZE=$(ls -lh verse_client_modular | awk '{print $5}')
    echo "  Executable: verse_client_modular ($SIZE)"
else
    echo "  Executable: not built"
fi

# Analysis
echo ""
echo "ANALYSIS:"
echo "---------"
if [ $ORIG_LINES -gt 0 ]; then
    INCREASE=$((TOTAL_LINES - ORIG_LINES))
    PERCENT=$((INCREASE * 100 / ORIG_LINES))
    echo "  Code increase: $INCREASE lines ($PERCENT%)"
    echo "  Main file reduction: $((ORIG_LINES - MAIN_LINES)) lines ($(((ORIG_LINES - MAIN_LINES) * 100 / ORIG_LINES))%)"
fi

echo ""
echo "BENEFITS:"
echo "---------"
echo "  ✓ Modular architecture"
echo "  ✓ Single responsibility per file"
echo "  ✓ Easier testing and maintenance"
echo "  ✓ Faster incremental compilation"
echo "  ✓ Better code organization"

# World modules
echo ""
echo "WORLD MODULES (extracted from world.c):"
echo "--------------------------------------"
WORLD_TOTAL=0
for module in world_core world_voxel world_noise world_serialize world_physics \
              world_generation_dispatch world_generation_simple world_generation_wilderness_simple \
              world_generation_scoured world_generation_labyrinth world_generation_wfc_town; do
    if [ -f "${module}.c" ]; then
        LINES=$(count_lines "${module}.c")
        WORLD_TOTAL=$((WORLD_TOTAL + LINES))
        printf "  %-35s %4d lines\n" "$module:" "$LINES"
    fi
done
echo "  ------------------------------------"
echo "  Total extracted from world.c:      $WORLD_TOTAL lines"

WORLD_ORIG=$(count_lines "world.c")
if [ $WORLD_ORIG -gt 0 ]; then
    PERCENT=$((WORLD_TOTAL * 100 / WORLD_ORIG))
    echo "  Extraction rate: $PERCENT% of world.c"
fi
