#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <SDL2/SDL.h>
#include "world.h"
#include "universe.h"

// Configuration
#define UNIVERSE_SIZE 5   // 3x3 worlds (generate 9 worlds)
#define VOXEL_PIXEL_SIZE 32  // 32 pixels per voxel
#define WORLD_SIZE 128  // 128x128x128 worlds

// Function prototypes
bool generate_universe_map(void);
bool create_unified_world(World **unified_world);
bool extract_top_layers(World *unified_world);
bool render_world_to_bitmap(const World *world, const char *filename);
bool render_float_map_to_bitmap(const float *map, uint32_t width, uint32_t height, const char *filename);
bool render_height_to_bitmap(const int16_t *heightmap, uint32_t width, uint32_t height, const char *filename);
void cleanup_resources(void);

// Global resources
World *unified_world = NULL;
World *wilderness_worlds[UNIVERSE_SIZE][UNIVERSE_SIZE] = {0};
char base_seed[65] = "universe_map_generation_seed_2024";

// Layer buffers
static float *occupancy_map = NULL;   // [0,1]
static float *rarity_map = NULL;      // [0,1]
static float *stone_map = NULL;       // [0,1]
static int16_t *top_height = NULL;    // topmost solid z or -1

int main(int argc, char *argv[])
{
    printf("Universe Map Generator\n");
    printf("Generating %dx%d wilderness worlds and creating unified map...\n", UNIVERSE_SIZE, UNIVERSE_SIZE);

    if (!generate_universe_map()) {
        fprintf(stderr, "Failed to generate universe map\n");
        cleanup_resources();
        return 1;
    }

    printf("Universe map generated successfully!\n");
    printf("Output: assets/universe_map.bmp\n");

    cleanup_resources();
    return 0;
}

bool generate_universe_map(void)
{
    // Initialize SDL for bitmap rendering
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    // Create unified world
    if (!create_unified_world(&unified_world)) {
        fprintf(stderr, "Failed to create unified world\n");
        return false;
    }

    // Extract top layers and feature maps from all wilderness worlds
    if (!extract_top_layers(unified_world)) {
        fprintf(stderr, "Failed to extract top layers\n");
        return false;
    }

    // Render top voxel layer
    if (!render_world_to_bitmap(unified_world, "assets/universe_top_voxel.bmp")) {
        fprintf(stderr, "Failed to render top voxel map\n");
        return false;
    }

    // Render additional layers
    uint32_t uW = UNIVERSE_SIZE * WORLD_SIZE;
    uint32_t uH = UNIVERSE_SIZE * WORLD_SIZE;
    if (occupancy_map && !render_float_map_to_bitmap(occupancy_map, uW, uH, "assets/universe_occupancy.bmp")) return false;
    if (rarity_map && !render_float_map_to_bitmap(rarity_map, uW, uH, "assets/universe_rarity.bmp")) return false;
    if (stone_map && !render_float_map_to_bitmap(stone_map, uW, uH, "assets/universe_stonefield.bmp")) return false;
    if (top_height && !render_height_to_bitmap(top_height, uW, uH, "assets/universe_height.bmp")) return false;

    return true;
}

bool create_unified_world(World **unified_world)
{
    // Calculate unified world dimensions
    uint32_t unified_width = UNIVERSE_SIZE * WORLD_SIZE;
    uint32_t unified_height = UNIVERSE_SIZE * WORLD_SIZE;
    uint32_t unified_depth = 1;  // Only top layer

    printf("Creating unified world: %dx%dx%d\n", unified_width, unified_height, unified_depth);

    // Create the unified world
    *unified_world = world_create(unified_width, unified_height, unified_depth);
    if (!*unified_world) {
        fprintf(stderr, "Failed to create unified world\n");
        return false;
    }

    // Initialize with air
    for (uint32_t z = 0; z < unified_depth; z++) {
        for (uint32_t y = 0; y < unified_height; y++) {
            for (uint32_t x = 0; x < unified_width; x++) {
                world_set_voxel(*unified_world, x, y, z, VOXEL_AIR);
            }
        }
    }

    return true;
}

static int find_top_solid_z(const World *w, uint32_t x, uint32_t y)
{
    for (int z = (int)w->depth - 1; z >= 0; z--)
    {
        const Voxel *v = world_voxel_cptr_fast((World *)w, (int)x, (int)y, z);
        if (v && v->type != VOXEL_AIR) return z;
    }
    return -1;
}

bool extract_top_layers(World *unified_world)
{
    printf("Extracting top layers from %dx%d wilderness worlds...\n", UNIVERSE_SIZE, UNIVERSE_SIZE);

    // Allocate layer buffers
    uint32_t uW = UNIVERSE_SIZE * WORLD_SIZE;
    uint32_t uH = UNIVERSE_SIZE * WORLD_SIZE;
    occupancy_map = (float *)malloc(sizeof(float) * uW * uH);
    rarity_map    = (float *)malloc(sizeof(float) * uW * uH);
    stone_map     = (float *)malloc(sizeof(float) * uW * uH);
    top_height    = (int16_t *)malloc(sizeof(int16_t) * uW * uH);
    if (!occupancy_map || !rarity_map || !stone_map || !top_height)
    {
        fprintf(stderr, "Failed to allocate layer buffers\n");
        return false;
    }

    // Initialize universe and use consistent seed
    Universe uni;
    if (!universe_init(&uni, base_seed, 0, 1))
    {
        fprintf(stderr, "Failed to initialize universe for map generation\n");
        return false;
    }

    // Generate and process each wilderness world
    for (int world_x = 0; world_x < UNIVERSE_SIZE; world_x++) {
        for (int world_y = 0; world_y < UNIVERSE_SIZE; world_y++) {
            printf("Processing world (%d, %d)...\n", world_x, world_y);

            // Generate seed for this world coordinate
            char world_seed[65];
            snprintf(world_seed, sizeof(world_seed), "%s_%d_%d", base_seed, world_x, world_y);

            // Create wilderness world
            World *wilderness = world_create(WORLD_SIZE, WORLD_SIZE, WORLD_SIZE);
            if (!wilderness) {
                fprintf(stderr, "Failed to create wilderness world at (%d, %d)\n", world_x, world_y);
                continue;
            }

            // Place then generate for consistent universe coordinates
            if (!universe_place(&uni, world_x, world_y, 0, wilderness))
            {
                fprintf(stderr, "Failed to place world (%d,%d)\n", world_x, world_y);
                world_destroy(wilderness);
                continue;
            }
            world_generate_with_type(wilderness, world_seed, WORLD_TYPE_WILDERNESS);

            // For each column: compute top, occupancy/rarity/stone samples and save
            for (uint32_t local_x = 0; local_x < WORLD_SIZE; local_x++) {
                for (uint32_t local_y = 0; local_y < WORLD_SIZE; local_y++) {
                    uint32_t unified_x = (uint32_t)world_x * WORLD_SIZE + local_x;
                    uint32_t unified_y = (uint32_t)world_y * WORLD_SIZE + local_y;

                    int tz = find_top_solid_z(wilderness, local_x, local_y);
                    top_height[unified_y * uW + unified_x] = (int16_t)tz;

                    VoxelType top_voxel_type = VOXEL_AIR;
                    if (tz >= 0)
                    {
                        const Voxel *voxel = world_voxel_cptr_fast(wilderness, local_x, local_y, tz);
                        top_voxel_type = voxel ? voxel->type : VOXEL_AIR;
                    }

                    // Write top voxel type into unified layer
                    if (top_voxel_type != VOXEL_AIR)
                        world_set_voxel(unified_world, unified_x, unified_y, 0, top_voxel_type);

                    // Sample feature fields at surface (or z=0 if none)
                    int sample_z = (tz >= 0) ? tz : 0;
                    float occ = world_sample_occupancy_noise(wilderness, (int)local_x, (int)local_y, sample_z);
                    float rar = world_sample_rarity_column(wilderness, (int)local_x, (int)local_y);
                    float stn = world_sample_stone_field(wilderness, (int)local_x, (int)local_y, sample_z);
                    // Clamp to [0,1]
                    if (occ < 0.0f) occ = 0.0f; if (occ > 1.0f) occ = 1.0f;
                    if (rar < 0.0f) rar = 0.0f; if (rar > 1.0f) rar = 1.0f;
                    if (stn < 0.0f) stn = 0.0f; if (stn > 1.0f) stn = 1.0f;
                    occupancy_map[unified_y * uW + unified_x] = occ;
                    rarity_map[unified_y * uW + unified_x] = rar;
                    stone_map[unified_y * uW + unified_x] = stn;
                }
            }

            // Store reference and clean up
            wilderness_worlds[world_x][world_y] = wilderness;
        }
    }

    printf("Top layer extraction completed\n");
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

bool render_float_map_to_bitmap(const float *map, uint32_t width, uint32_t height, const char *filename)
{
    if (!map || !filename) return false;
    int bitmap_width = (int)width;
    int bitmap_height = (int)height;
    SDL_Surface *surface = SDL_CreateRGBSurface(0, bitmap_width, bitmap_height, 32,
                                               0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surface) return false;
    if (SDL_LockSurface(surface) < 0) { SDL_FreeSurface(surface); return false; }
    uint32_t *pixels = (uint32_t *)surface->pixels;
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            float v = map[y * width + x];
            if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
            uint8_t g = (uint8_t)lrintf(v * 255.0f);
            uint32_t color = (0xFF << 24) | (g << 16) | (g << 8) | g;
            pixels[y * bitmap_width + x] = color;
        }
    }
    SDL_UnlockSurface(surface);
    bool ok = SDL_SaveBMP(surface, filename) == 0;
    SDL_FreeSurface(surface);
    if (ok) printf("Bitmap saved: %s (%ux%u)\n", filename, width, height);
    return ok;
}

bool render_height_to_bitmap(const int16_t *heightmap, uint32_t width, uint32_t height, const char *filename)
{
    if (!heightmap || !filename) return false;
    int bitmap_width = (int)width;
    int bitmap_height = (int)height;
    SDL_Surface *surface = SDL_CreateRGBSurface(0, bitmap_width, bitmap_height, 32,
                                               0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surface) return false;
    if (SDL_LockSurface(surface) < 0) { SDL_FreeSurface(surface); return false; }
    uint32_t *pixels = (uint32_t *)surface->pixels;
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            int16_t hz = heightmap[y * width + x];
            uint8_t g = 0;
            if (hz >= 0)
            {
                // Normalize to 0..255 with WORLD_SIZE as max
                float v = (float)hz / (float)WORLD_SIZE;
                if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
                g = (uint8_t)lrintf(v * 255.0f);
            }
            uint32_t color = (0xFF << 24) | (g << 16) | (g << 8) | g;
            pixels[y * bitmap_width + x] = color;
        }
    }
    SDL_UnlockSurface(surface);
    bool ok = SDL_SaveBMP(surface, filename) == 0;
    SDL_FreeSurface(surface);
    if (ok) printf("Bitmap saved: %s (%ux%u)\n", filename, width, height);
    return ok;
}

void cleanup_resources(void)
{
    // Clean up unified world
    if (unified_world) {
        world_destroy(unified_world);
        unified_world = NULL;
    }

    // Clean up wilderness worlds
    for (int x = 0; x < UNIVERSE_SIZE; x++) {
        for (int y = 0; y < UNIVERSE_SIZE; y++) {
            if (wilderness_worlds[x][y]) {
                world_destroy(wilderness_worlds[x][y]);
                wilderness_worlds[x][y] = NULL;
            }
        }
    }

    // Quit SDL
    SDL_Quit();
}
