#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "world.h"
#include "constants.h"
#include "universe.h"

// Edge detection structure
typedef struct {
    int x, y;
    bool is_edge;
    int edge_type; // 0=none, 1=horizontal, 2=vertical, 3=both
} EdgePoint;

// Edge detection results
typedef struct {
    EdgePoint *edges;
    int edge_count;
    int total_points;
    int horizontal_edges;
    int vertical_edges;
    int corner_edges;
} EdgeAnalysis;

// Function prototypes
static void generate_wilderness_slice(World *world, const char *seed);
static EdgeAnalysis *analyze_edges(const World *world, int slice_z);
static void detect_horizontal_edges(const World *world, int slice_z, EdgePoint *edges);
static void detect_vertical_edges(const World *world, int slice_z, EdgePoint *edges);
static void print_edge_analysis(const EdgeAnalysis *analysis, const char *seed);
static void save_slice_to_file(const World *world, int slice_z, const char *filename);
static void cleanup_edge_analysis(EdgeAnalysis *analysis);

// Generate a wilderness world and extract the first slice above bedrock (z=1)
static void generate_wilderness_slice(World *world, const char *seed)
{
    if (!world) {
        printf("Error: World is NULL\n");
        return;
    }

    printf("Generating wilderness world with seed: %s\n", seed);

    // Generate the wilderness world
    world_generate_with_type(world, seed, WORLD_TYPE_WILDERNESS);

    printf("World generated successfully. Dimensions: %dx%dx%d\n",
           world->width, world->height, world->depth);
}

// Analyze edges in a specific slice
static EdgeAnalysis *analyze_edges(const World *world, int slice_z)
{
    if (!world || slice_z < 0 || slice_z >= (int)world->depth) {
        printf("Error: Invalid world or slice_z\n");
        return NULL;
    }

    EdgeAnalysis *analysis = malloc(sizeof(EdgeAnalysis));
    if (!analysis) {
        printf("Error: Failed to allocate memory for edge analysis\n");
        return NULL;
    }

    int width = (int)world->width;
    int height = (int)world->height;
    analysis->total_points = width * height;
    analysis->edge_count = 0;
    analysis->horizontal_edges = 0;
    analysis->vertical_edges = 0;
    analysis->corner_edges = 0;

    // Allocate edge points array
    analysis->edges = calloc(analysis->total_points, sizeof(EdgePoint));
    if (!analysis->edges) {
        printf("Error: Failed to allocate memory for edge points\n");
        free(analysis);
        return NULL;
    }

    // Initialize edge points
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            analysis->edges[idx].x = x;
            analysis->edges[idx].y = y;
            analysis->edges[idx].is_edge = false;
            analysis->edges[idx].edge_type = 0;
        }
    }

    // Detect horizontal and vertical edges
    detect_horizontal_edges(world, slice_z, analysis->edges);
    detect_vertical_edges(world, slice_z, analysis->edges);

    // Count edges and categorize
    for (int i = 0; i < analysis->total_points; i++) {
        if (analysis->edges[i].is_edge) {
            analysis->edge_count++;
            if (analysis->edges[i].edge_type == 1) {
                analysis->horizontal_edges++;
            } else if (analysis->edges[i].edge_type == 2) {
                analysis->vertical_edges++;
            } else if (analysis->edges[i].edge_type == 3) {
                analysis->corner_edges++;
            }
        }
    }

    return analysis;
}

// Detect horizontal edges (axis-aligned cutoffs along X-axis)
static void detect_horizontal_edges(const World *world, int slice_z, EdgePoint *edges)
{
    int width = (int)world->width;
    int height = (int)world->height;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width - 1; x++) {
            // Get voxel types at current and next position
            const Voxel *v1 = world_voxel_cptr_fast(world, x, y, slice_z);
            const Voxel *v2 = world_voxel_cptr_fast(world, x + 1, y, slice_z);

            if (!v1 || !v2) continue;

            // Check for solid-to-air or air-to-solid transitions
            bool v1_solid = (v1->type != VOXEL_AIR);
            bool v2_solid = (v2->type != VOXEL_AIR);

            if (v1_solid != v2_solid) {
                // Found a horizontal edge
                int idx1 = y * width + x;
                int idx2 = y * width + (x + 1);

                edges[idx1].is_edge = true;
                edges[idx1].edge_type |= 1; // horizontal

                edges[idx2].is_edge = true;
                edges[idx2].edge_type |= 1; // horizontal
            }
        }
    }
}

// Detect vertical edges (axis-aligned cutoffs along Y-axis)
static void detect_vertical_edges(const World *world, int slice_z, EdgePoint *edges)
{
    int width = (int)world->width;
    int height = (int)world->height;

    for (int y = 0; y < height - 1; y++) {
        for (int x = 0; x < width; x++) {
            // Get voxel types at current and next position
            const Voxel *v1 = world_voxel_cptr_fast(world, x, y, slice_z);
            const Voxel *v2 = world_voxel_cptr_fast(world, x, y + 1, slice_z);

            if (!v1 || !v2) continue;

            // Check for solid-to-air or air-to-solid transitions
            bool v1_solid = (v1->type != VOXEL_AIR);
            bool v2_solid = (v2->type != VOXEL_AIR);

            if (v1_solid != v2_solid) {
                // Found a vertical edge
                int idx1 = y * width + x;
                int idx2 = (y + 1) * width + x;

                edges[idx1].is_edge = true;
                edges[idx1].edge_type |= 2; // vertical

                edges[idx2].is_edge = true;
                edges[idx2].edge_type |= 2; // vertical
            }
        }
    }
}

// Print edge analysis results
static void print_edge_analysis(const EdgeAnalysis *analysis, const char *seed)
{
    if (!analysis) {
        printf("Error: Analysis is NULL\n");
        return;
    }

    printf("\n=== EDGE DETECTION ANALYSIS ===\n");
    printf("Seed: %s\n", seed);
    printf("Total points analyzed: %d\n", analysis->total_points);
    printf("Total edges found: %d\n", analysis->edge_count);
    printf("Horizontal edges: %d\n", analysis->horizontal_edges);
    printf("Vertical edges: %d\n", analysis->vertical_edges);
    printf("Corner edges (both): %d\n", analysis->corner_edges);

    if (analysis->total_points > 0) {
        float edge_percentage = (float)analysis->edge_count / analysis->total_points * 100.0f;
        printf("Edge density: %.2f%%\n", edge_percentage);
    }

        // Look for contiguous lines (potential axis-aligned cutoff issues)
    printf("\n=== CONTIGUOUS LINE ANALYSIS ===\n");

    // This is a simplified check - in a full implementation, you'd want to
    // trace actual contiguous lines of edges
    if (analysis->horizontal_edges > analysis->total_points * 0.1f) {
        printf("WARNING: High horizontal edge density (%.2f%%) - potential axis-aligned cutoff issues\n",
               (float)analysis->horizontal_edges / analysis->total_points * 100.0f);
    }

    if (analysis->vertical_edges > analysis->total_points * 0.1f) {
        printf("WARNING: High vertical edge density (%.2f%%) - potential axis-aligned cutoff issues\n",
               (float)analysis->vertical_edges / analysis->total_points * 100.0f);
    }

    // Print first few edge locations for debugging
    printf("\nFirst 10 edge locations:\n");
    int printed = 0;
    for (int i = 0; i < analysis->total_points && printed < 10; i++) {
        if (analysis->edges[i].is_edge) {
            printf("  Edge at (%d, %d) - Type: %s%s\n",
                   analysis->edges[i].x, analysis->edges[i].y,
                   (analysis->edges[i].edge_type & 1) ? "H" : "",
                   (analysis->edges[i].edge_type & 2) ? "V" : "");
            printed++;
        }
    }
}

// Save slice data to a text file for inspection
static void save_slice_to_file(const World *world, int slice_z, const char *filename)
{
    if (!world || slice_z < 0 || slice_z >= (int)world->depth) {
        printf("Error: Invalid world or slice_z for file save\n");
        return;
    }

    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("Error: Failed to open file %s for writing\n", filename);
        return;
    }

    fprintf(file, "Wilderness World Slice at Z=%d\n", slice_z);
    fprintf(file, "Dimensions: %dx%d\n", world->width, world->height);
    fprintf(file, "Voxel types: A=Air, S=Stone, D=Soil, G=Grass, B=Bedrock, M=Magma, b=Basalt, g=Granite, l=Limestone, s=Sandstone, c=Coal, i=Iron, p=Copper, o=Gold, v=Silver, C=Crystal, X=Other\n\n");

    // Print slice data
    for (int y = 0; y < (int)world->height; y++) {
        for (int x = 0; x < (int)world->width; x++) {
            const Voxel *v = world_voxel_cptr_fast(world, x, y, slice_z);
            if (v) {
                char symbol = '?';
                switch (v->type) {
                    case VOXEL_AIR: symbol = 'A'; break;
                    case VOXEL_STONE: symbol = 'S'; break;
                    case VOXEL_SOIL: symbol = 'D'; break;
                    case VOXEL_GRASS: symbol = 'G'; break;
                    case VOXEL_BEDROCK: symbol = 'B'; break;
                    case VOXEL_WATER: symbol = 'W'; break;
                    case VOXEL_SAND: symbol = 'N'; break;
                    case VOXEL_MAGMA: symbol = 'M'; break;
                    case VOXEL_STONE_BASALT: symbol = 'b'; break;
                    case VOXEL_STONE_GRANITE: symbol = 'g'; break;
                    case VOXEL_STONE_LIMESTONE: symbol = 'l'; break;
                    case VOXEL_STONE_SANDSTONE: symbol = 's'; break;
                    case VOXEL_ORE_COAL: symbol = 'c'; break;
                    case VOXEL_ORE_IRON: symbol = 'i'; break;
                    case VOXEL_ORE_COPPER: symbol = 'p'; break;
                    case VOXEL_ORE_GOLD: symbol = 'o'; break;
                    case VOXEL_ORE_SILVER: symbol = 'v'; break;
                    case VOXEL_CRYSTAL: symbol = 'C'; break;
                    default: symbol = 'X'; break;
                }
                fprintf(file, "%c", symbol);
            } else {
                fprintf(file, "?");
            }
        }
        fprintf(file, "\n");
    }

    fclose(file);
    printf("Slice data saved to %s\n", filename);
}

// Clean up edge analysis memory
static void cleanup_edge_analysis(EdgeAnalysis *analysis)
{
    if (analysis) {
        if (analysis->edges) {
            free(analysis->edges);
        }
        free(analysis);
    }
}

// Generate a random seed if none provided
static void generate_random_seed(char *seed_buffer, size_t buffer_size)
{
    unsigned int t = (unsigned int)time(NULL);
    for (int i = 0; i < 32 && i * 2 + 1 < (int)buffer_size; i++) {
        t = t * 1103515245u + 12345u;
        sprintf(seed_buffer + i * 2, "%02x", (unsigned)(t & 0xFF));
    }
    seed_buffer[64] = '\0';
}

int main(int argc, char **argv)
{
    const char *seed = NULL;
    bool save_slice = false;
    const char *output_file = "wilderness_slice.txt";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--seed") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc) {
            seed = argv[++i];
        } else if (strcmp(argv[i], "--save") == 0) {
            save_slice = true;
        } else if ((strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--seed SEED] [--save] [--output FILE] [--help]\n", argv[0]);
            printf("  --seed SEED    Use specific seed for world generation\n");
            printf("  --save         Save slice data to file\n");
            printf("  --output FILE  Output file for slice data (default: wilderness_slice.txt)\n");
            printf("  --help         Show this help message\n");
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            printf("Use --help for usage information\n");
            return 1;
        }
    }

    // Generate seed if not provided
    char seed_buffer[65];
    if (!seed || strlen(seed) == 0) {
        generate_random_seed(seed_buffer, sizeof(seed_buffer));
        seed = seed_buffer;
    } else {
        // Copy provided seed to buffer
        size_t len = strlen(seed);
        if (len > 64) len = 64;
        memcpy(seed_buffer, seed, len);
        seed_buffer[len] = '\0';
        seed = seed_buffer;
    }

    printf("Wilderness Slice Analyzer\n");
    printf("========================\n");

    // Create world
    World *world = world_create(WORLD_SIZE_X, WORLD_SIZE_Y, WORLD_SIZE_Z);
    if (!world) {
        printf("Error: Failed to create world\n");
        return 1;
    }

    // Generate wilderness world
    generate_wilderness_slice(world, seed);

    // Analyze multiple slices to find edge patterns
    int slices_to_analyze[] = {1, 5, 10, 20, 30, 50, 70, 90, 110};
    int num_slices = sizeof(slices_to_analyze) / sizeof(slices_to_analyze[0]);

    for (int i = 0; i < num_slices; i++) {
        int slice_z = slices_to_analyze[i];
        if (slice_z >= (int)world->depth) continue;

        printf("\n=== ANALYZING SLICE Z=%d ===\n", slice_z);

        EdgeAnalysis *analysis = analyze_edges(world, slice_z);
        if (!analysis) {
            printf("Error: Failed to analyze edges for slice Z=%d\n", slice_z);
            continue;
        }

        // Print analysis results
        print_edge_analysis(analysis, seed);

        // Save slice to file if requested (only for first slice)
        if (save_slice && i == 0) {
            save_slice_to_file(world, slice_z, output_file);
        }

        // Cleanup
        cleanup_edge_analysis(analysis);
    }
    world_destroy(world);

    printf("\nAnalysis complete!\n");
    return 0;
}
