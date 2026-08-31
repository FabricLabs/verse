#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <SDL.h>

// Configuration
#define UNIVERSE_SIZE 32  // 32x32 worlds
#define VOXEL_PIXEL_SIZE 32  // 32 pixels per voxel
#define WORLD_SIZE 64  // Assuming 64x64x64 worlds

// Simple voxel types for demonstration
typedef enum {
    VOXEL_AIR = 0,
    VOXEL_GRASS,
    VOXEL_STONE,
    VOXEL_WATER,
    VOXEL_SAND,
    VOXEL_WOOD,
    VOXEL_LEAVES,
    VOXEL_MAGMA,
    VOXEL_COUNT
} SimpleVoxelType;

// Simple voxel structure
typedef struct {
    SimpleVoxelType type;
} SimpleVoxel;

// Simple world structure
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    SimpleVoxel *voxels;
} SimpleWorld;

// Function prototypes
bool generate_simple_universe_map(void);
SimpleWorld *create_simple_world(uint32_t width, uint32_t height, uint32_t depth);
void destroy_simple_world(SimpleWorld *world);
void generate_wilderness_pattern(SimpleWorld *world, int world_x, int world_y);
bool render_simple_world_to_bitmap(const SimpleWorld *world, const char *filename);
void get_voxel_color(SimpleVoxelType type, uint8_t *r, uint8_t *g, uint8_t *b);

int main(int argc, char *argv[])
{
    printf("Simple Universe Map Generator\n");
    printf("Generating %dx%d wilderness worlds and creating unified map...\n", UNIVERSE_SIZE, UNIVERSE_SIZE);

    if (!generate_simple_universe_map()) {
        fprintf(stderr, "Failed to generate universe map\n");
        return 1;
    }

    printf("Universe map generated successfully!\n");
    printf("Output: assets/universe_map.bmp\n");

    return 0;
}

bool generate_simple_universe_map(void)
{
    // Initialize SDL for bitmap rendering
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    // Calculate unified world dimensions
    uint32_t unified_width = UNIVERSE_SIZE * WORLD_SIZE;
    uint32_t unified_height = UNIVERSE_SIZE * WORLD_SIZE;
    uint32_t unified_depth = 1;  // Only top layer

    printf("Creating unified world: %dx%dx%d\n", unified_width, unified_height, unified_depth);

    // Create the unified world
    SimpleWorld *unified_world = create_simple_world(unified_width, unified_height, unified_depth);
    if (!unified_world) {
        fprintf(stderr, "Failed to create unified world\n");
        SDL_Quit();
        return false;
    }

    // Initialize with air
    for (uint32_t z = 0; z < unified_depth; z++) {
        for (uint32_t y = 0; y < unified_height; y++) {
            for (uint32_t x = 0; x < unified_width; x++) {
                size_t index = z * unified_height * unified_width + y * unified_width + x;
                unified_world->voxels[index].type = VOXEL_AIR;
            }
        }
    }

    printf("Generating wilderness patterns for %dx%d worlds...\n", UNIVERSE_SIZE, UNIVERSE_SIZE);

    // Generate wilderness patterns for each world
    for (int world_x = 0; world_x < UNIVERSE_SIZE; world_x++) {
        for (int world_y = 0; world_y < UNIVERSE_SIZE; world_y++) {
            printf("Processing world (%d, %d)...\n", world_x, world_y);

            // Generate pattern for this world
            generate_wilderness_pattern(unified_world, world_x, world_y);
        }
    }

    // Render to bitmap
    if (!render_simple_world_to_bitmap(unified_world, "assets/universe_map.bmp")) {
        fprintf(stderr, "Failed to render world to bitmap\n");
        destroy_simple_world(unified_world);
        SDL_Quit();
        return false;
    }

    destroy_simple_world(unified_world);
    SDL_Quit();
    return true;
}

SimpleWorld *create_simple_world(uint32_t width, uint32_t height, uint32_t depth)
{
    SimpleWorld *world = malloc(sizeof(SimpleWorld));
    if (!world) return NULL;

    world->width = width;
    world->height = height;
    world->depth = depth;

    size_t voxel_count = (size_t)width * height * depth;
    world->voxels = calloc(voxel_count, sizeof(SimpleVoxel));
    if (!world->voxels) {
        free(world);
        return NULL;
    }

    return world;
}

void destroy_simple_world(SimpleWorld *world)
{
    if (world) {
        free(world->voxels);
        free(world);
    }
}

void generate_wilderness_pattern(SimpleWorld *world, int world_x, int world_y)
{
    // Simple noise-based pattern generation
    uint32_t base_x = world_x * WORLD_SIZE;
    uint32_t base_y = world_y * WORLD_SIZE;

    for (uint32_t local_x = 0; local_x < WORLD_SIZE; local_x++) {
        for (uint32_t local_y = 0; local_y < WORLD_SIZE; local_y++) {
            uint32_t global_x = base_x + local_x;
            uint32_t global_y = base_y + local_y;

            // Simple noise function using coordinates
            float noise1 = sin(global_x * 0.1f) * cos(global_y * 0.1f);
            float noise2 = sin(global_x * 0.05f + global_y * 0.03f);
            float combined_noise = (noise1 + noise2) * 0.5f;

            // Add some randomness based on world position
            float world_noise = sin(world_x * 0.2f) * cos(world_y * 0.2f);
            combined_noise += world_noise * 0.3f;

            // Determine voxel type based on noise
            SimpleVoxelType voxel_type = VOXEL_AIR;

            if (combined_noise > 0.3f) {
                voxel_type = VOXEL_GRASS;
            } else if (combined_noise > 0.1f) {
                voxel_type = VOXEL_STONE;
            } else if (combined_noise > -0.1f) {
                voxel_type = VOXEL_SAND;
            } else if (combined_noise > -0.3f) {
                voxel_type = VOXEL_WATER;
            } else {
                // Rare magma patches
                voxel_type = VOXEL_MAGMA;
            }

            // Add some tree patches
            float tree_noise = sin(global_x * 0.08f) * cos(global_y * 0.08f);
            if (tree_noise > 0.6f && voxel_type == VOXEL_GRASS) {
                voxel_type = VOXEL_WOOD;
            } else if (tree_noise > 0.4f && voxel_type == VOXEL_GRASS) {
                voxel_type = VOXEL_LEAVES;
            }

            // Set the voxel
            size_t index = global_y * world->width + global_x;
            if (index < (size_t)world->width * world->height) {
                world->voxels[index].type = voxel_type;
            }
        }
    }
}

bool render_simple_world_to_bitmap(const SimpleWorld *world, const char *filename)
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
            size_t voxel_index = y * world->width + x;
            SimpleVoxelType voxel_type = world->voxels[voxel_index].type;

            // Get color for this voxel type
            uint8_t r, g, b;
            get_voxel_color(voxel_type, &r, &g, &b);

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

void get_voxel_color(SimpleVoxelType type, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (type) {
        case VOXEL_AIR:
            *r = 135; *g = 206; *b = 235; // Sky blue
            break;
        case VOXEL_GRASS:
            *r = 124; *g = 252; *b = 0;   // Lawn green
            break;
        case VOXEL_STONE:
            *r = 128; *g = 128; *b = 128; // Gray
            break;
        case VOXEL_WATER:
            *r = 30;  *g = 144; *b = 255; // Dodger blue
            break;
        case VOXEL_SAND:
            *r = 244; *g = 164; *b = 96;  // Sandy brown
            break;
        case VOXEL_WOOD:
            *r = 139; *g = 69;  *b = 19;  // Saddle brown
            break;
        case VOXEL_LEAVES:
            *r = 34;  *g = 139; *b = 34;  // Forest green
            break;
        case VOXEL_MAGMA:
            *r = 255; *g = 69;  *b = 0;   // Red orange
            break;
        default:
            *r = 0;   *g = 0;   *b = 0;   // Black
            break;
    }
}
