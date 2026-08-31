#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <SDL.h>
#include "world.h"
#include "universe.h"

// Configuration - smaller for testing
#define UNIVERSE_SIZE 16   // 8x8 worlds instead of 32x32
#define VOXEL_PIXEL_SIZE 1  // 8 pixels per voxel instead of 32
#define WORLD_SIZE 128    // 16x16 worlds instead of 64x64

// Use the actual world system

// Function prototypes
bool generate_small_universe_map(void);
bool create_unified_world(void);
bool extract_all_layers(World *unified_world);
bool render_world_to_bitmap(const World *world, const char *filename);
bool render_all_z_slices(const World *world, const char *base_filename);
void cleanup_resources(void);

// Global resources
World *unified_world = NULL;
World *wilderness_worlds[UNIVERSE_SIZE][UNIVERSE_SIZE] = {0};
char base_seed[65] = "universe_map_generation_seed_2024";

int main(int argc, char *argv[])
{
    printf("Small Universe Map Generator\n");
    printf("Generating %dx%d wilderness worlds and creating unified map...\n", UNIVERSE_SIZE, UNIVERSE_SIZE);

    if (!generate_small_universe_map()) {
        fprintf(stderr, "Failed to generate universe map\n");
        cleanup_resources();
        return 1;
    }

    printf("Universe map generated successfully!\n");
    printf("Output: assets/small_universe_map.bmp (top layer)\n");
    printf("Output: assets/slice_z_000.bmp to assets/slice_z_127.bmp (all Z slices)\n");

    cleanup_resources();
    return 0;
}

bool generate_small_universe_map(void)
{
    // Initialize SDL for bitmap rendering
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    // Create unified world
    if (!create_unified_world()) {
        fprintf(stderr, "Failed to create unified world\n");
        SDL_Quit();
        return false;
    }

    // Extract all layers from all wilderness worlds
    if (!extract_all_layers(unified_world)) {
        fprintf(stderr, "Failed to extract all layers\n");
        SDL_Quit();
        return false;
    }

    // Render top layer to bitmap
    if (!render_world_to_bitmap(unified_world, "assets/small_universe_map.bmp")) {
        fprintf(stderr, "Failed to render world to bitmap\n");
        SDL_Quit();
        return false;
    }

    // Render all Z slices as separate bitmaps
    if (!render_all_z_slices(unified_world, "assets/slice_z")) {
        fprintf(stderr, "Failed to render Z slices\n");
        SDL_Quit();
        return false;
    }

    SDL_Quit();
    return true;
}

bool create_unified_world(void)
{
    // Calculate unified world dimensions
    uint32_t unified_width = UNIVERSE_SIZE * WORLD_SIZE;
    uint32_t unified_height = UNIVERSE_SIZE * WORLD_SIZE;
    uint32_t unified_depth = WORLD_SIZE;  // Full depth for all layers

    printf("Creating unified world: %dx%dx%d\n", unified_width, unified_height, unified_depth);

    // Create the unified world
    unified_world = world_create(unified_width, unified_height, unified_depth);
    if (!unified_world) {
        fprintf(stderr, "Failed to create unified world\n");
        return false;
    }

    // Initialize with air
    for (uint32_t z = 0; z < unified_depth; z++) {
        for (uint32_t y = 0; y < unified_height; y++) {
            for (uint32_t x = 0; x < unified_width; x++) {
                world_set_voxel(unified_world, x, y, z, VOXEL_AIR);
            }
        }
    }

    return true;
}

bool extract_all_layers(World *unified_world)
{
    printf("Generating %dx%d wilderness worlds using unified noise API...\n", UNIVERSE_SIZE, UNIVERSE_SIZE);

    // Set the unified noise seed - this establishes the unified noise field
    // All worlds generated after this will use the same noise field
    char seed_str[32];
    snprintf(seed_str, sizeof(seed_str), "%llu", (unsigned long long)base_seed);
    world_set_universe_noise_seed(seed_str);

    // Create Universe with unified noise
    Universe universe;
    if (!universe_init(&universe, base_seed, 1, 0)) {
        fprintf(stderr, "Failed to initialize universe\n");
        return false;
    }

    // Generate each world slot independently using the unified noise API
    // This tests continuity across world boundaries
    for (int world_x = 0; world_x < UNIVERSE_SIZE; world_x++) {
        for (int world_y = 0; world_y < UNIVERSE_SIZE; world_y++) {
            printf("Generating world (%d, %d)...\n", world_x, world_y);

            // Create a new world
            World *wilderness = world_create(WORLD_SIZE, WORLD_SIZE, WORLD_SIZE);
            if (!wilderness) {
                fprintf(stderr, "Failed to create world at (%d, %d)\n", world_x, world_y);
                continue;
            }

            // Generate the world in universe context at the correct coordinates
            // This ensures each world samples the unified noise field at the right offset
            char seed_str[32];
            snprintf(seed_str, sizeof(seed_str), "%llu", (unsigned long long)base_seed);

            if (!world_generate_in_universe(wilderness, seed_str, WORLD_TYPE_WILDERNESS,
                                          &universe, (uint64_t)world_x, (uint64_t)world_y, 0)) {
                fprintf(stderr, "Failed to generate world at (%d, %d) in universe context\n", world_x, world_y);
                world_destroy(wilderness);
                continue;
            }

            // Store reference for later merging
            wilderness_worlds[world_x][world_y] = wilderness;
        }
    }

    // Now merge all worlds into the unified world for rendering
    printf("Merging all worlds into unified world for rendering...\n");
    for (int world_x = 0; world_x < UNIVERSE_SIZE; world_x++) {
        for (int world_y = 0; world_y < UNIVERSE_SIZE; world_y++) {
            World *wilderness = wilderness_worlds[world_x][world_y];
            if (!wilderness) continue;

            // Copy all layers from wilderness world to unified world
            for (uint32_t local_x = 0; local_x < WORLD_SIZE; local_x++) {
                for (uint32_t local_y = 0; local_y < WORLD_SIZE; local_y++) {
                    for (uint32_t local_z = 0; local_z < WORLD_SIZE; local_z++) {
                        const Voxel *voxel = world_voxel_cptr_fast(wilderness, local_x, local_y, local_z);
                        if (voxel) {
                            uint32_t unified_x = world_x * WORLD_SIZE + local_x;
                            uint32_t unified_y = world_y * WORLD_SIZE + local_y;
                            uint32_t unified_z = local_z;  // Direct Z mapping
                            world_set_voxel(unified_world, unified_x, unified_y, unified_z, voxel->type);
                        }
                    }
                }
            }
        }
    }

    // Clean up universe
    universe_free(&universe);

    printf("All worlds generated and merged successfully\n");
    return true;
}

bool render_world_to_bitmap(const World *world, const char *filename)
{
    if (!world || !filename) {
        fprintf(stderr, "Invalid parameters for bitmap rendering\n");
        return false;
    }

    printf("Rendering world to bitmap: %dx%d at %d pixels per voxel\n",
           world->width, world->height, VOXEL_PIXEL_SIZE);

    // Calculate bitmap dimensions
    int bitmap_width = world->width * VOXEL_PIXEL_SIZE;
    int bitmap_height = world->height * VOXEL_PIXEL_SIZE;

    printf("Bitmap dimensions: %dx%d pixels\n", bitmap_width, bitmap_height);

    // Create SDL surface for the bitmap
    SDL_Surface *surface = SDL_CreateRGBSurface(0, bitmap_width, bitmap_height, 32,
                                               0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surface) {
        fprintf(stderr, "Failed to create SDL surface: %s\n", SDL_GetError());
        return false;
    }

    // Lock surface for pixel access
    if (SDL_LockSurface(surface) < 0) {
        fprintf(stderr, "Failed to lock surface: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return false;
    }

    uint32_t *pixels = (uint32_t *)surface->pixels;

    // Render each voxel as a pixel block
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            const Voxel *voxel = world_voxel_cptr_fast(world, x, y, 0);
            VoxelType voxel_type = voxel ? voxel->type : VOXEL_AIR;

            // Get color for this voxel type
            uint8_t r, g, b;
            world_voxel_type_color(voxel_type, &r, &g, &b);

            // Create pixel color (RGBA)
            uint32_t pixel_color = (0xFF << 24) | (r << 16) | (g << 8) | b;

            // Fill the pixel block for this voxel
            for (int py = 0; py < VOXEL_PIXEL_SIZE; py++) {
                for (int px = 0; px < VOXEL_PIXEL_SIZE; px++) {
                    int pixel_x = x * VOXEL_PIXEL_SIZE + px;
                    int pixel_y = y * VOXEL_PIXEL_SIZE + py;

                    if (pixel_x < bitmap_width && pixel_y < bitmap_height) {
                        pixels[pixel_y * bitmap_width + pixel_x] = pixel_color;
                    }
                }
            }
        }
    }

    // Unlock surface
    SDL_UnlockSurface(surface);

    // Save as BMP
    if (SDL_SaveBMP(surface, filename) < 0) {
        fprintf(stderr, "Failed to save bitmap: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return false;
    }

    printf("Bitmap saved: %s (%dx%d pixels)\n", filename, bitmap_width, bitmap_height);

    SDL_FreeSurface(surface);
    return true;
}

bool render_all_z_slices(const World *world, const char *base_filename)
{
    if (!world || !base_filename) {
        fprintf(stderr, "Invalid parameters for Z slice rendering\n");
        return false;
    }

    printf("Rendering all Z slices: %dx%dx%d\n", world->width, world->height, world->depth);

    // Create assets directory if it doesn't exist
    system("mkdir -p assets");

    // Render each Z slice
    for (uint32_t z = 0; z < world->depth; z++) {
        char filename[256];
        snprintf(filename, sizeof(filename), "%s_%03d.bmp", base_filename, z);

        printf("Rendering Z slice %d/%d: %s\n", z + 1, world->depth, filename);

        // Calculate bitmap dimensions
        int bitmap_width = world->width * VOXEL_PIXEL_SIZE;
        int bitmap_height = world->height * VOXEL_PIXEL_SIZE;

        // Create SDL surface for the bitmap
        SDL_Surface *surface = SDL_CreateRGBSurface(0, bitmap_width, bitmap_height, 32,
                                                   0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        if (!surface) {
            fprintf(stderr, "Failed to create SDL surface for Z slice %d: %s\n", z, SDL_GetError());
            continue;
        }

        // Lock surface for pixel access
        if (SDL_LockSurface(surface) < 0) {
            fprintf(stderr, "Failed to lock surface for Z slice %d: %s\n", z, SDL_GetError());
            SDL_FreeSurface(surface);
            continue;
        }

        uint32_t *pixels = (uint32_t *)surface->pixels;

        // Render each voxel in this Z slice as a pixel block
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                const Voxel *voxel = world_voxel_cptr_fast(world, x, y, z);
                VoxelType voxel_type = voxel ? voxel->type : VOXEL_AIR;

                // Get color for this voxel type
                uint8_t r, g, b;
                world_voxel_type_color(voxel_type, &r, &g, &b);

                // Create pixel color (RGBA)
                uint32_t pixel_color = (0xFF << 24) | (r << 16) | (g << 8) | b;

                // Fill the pixel block for this voxel
                for (int py = 0; py < VOXEL_PIXEL_SIZE; py++) {
                    for (int px = 0; px < VOXEL_PIXEL_SIZE; px++) {
                        int pixel_x = x * VOXEL_PIXEL_SIZE + px;
                        int pixel_y = y * VOXEL_PIXEL_SIZE + py;

                        if (pixel_x < bitmap_width && pixel_y < bitmap_height) {
                            pixels[pixel_y * bitmap_width + pixel_x] = pixel_color;
                        }
                    }
                }
            }
        }

        // Unlock surface
        SDL_UnlockSurface(surface);

        // Save as BMP
        if (SDL_SaveBMP(surface, filename) < 0) {
            fprintf(stderr, "Failed to save Z slice %d bitmap: %s\n", z, SDL_GetError());
            SDL_FreeSurface(surface);
            continue;
        }

        printf("Z slice %d saved: %s (%dx%d pixels)\n", z, filename, bitmap_width, bitmap_height);
        SDL_FreeSurface(surface);
    }

    printf("All Z slices rendered successfully\n");
    return true;
}

void cleanup_resources(void)
{
    // Clean up unified world
    if (unified_world) {
        world_destroy(unified_world);
        unified_world = NULL;
    }

    // Note: wilderness_worlds are now managed by the Universe system
    // and will be cleaned up when universe_free() is called
    // No need to manually destroy them here
}
