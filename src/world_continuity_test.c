#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "universe.h"
#include "world.h"

static int find_top_solid_z(const World *world, uint32_t x, uint32_t y)
{
    for (int z = (int)world->depth - 1; z >= 0; z--)
    {
        Voxel *v = world_get_voxel((World *)world, x, y, (uint32_t)z);
        if (v && v->type != VOXEL_AIR) return z;
    }
    return -1;
}

int main(void)
{
    const char *seed = "1234567890abcdef";
    const uint32_t W = 64, H = 64, D = 64;

    Universe universe;
    if (!universe_init(&universe, seed, 0, 1))
    {
        printf("Failed to initialize universe\n");
        return 1;
    }

    World *w0 = world_create(W, H, D);
    World *w1 = world_create(W, H, D);
    if (!w0 || !w1)
    {
        printf("Failed to create worlds\n");
        return 1;
    }

    // Place worlds in the universe first to set universe_context and coords, then generate
    if (!universe_place(&universe, 0, 0, 0, w0))
    {
        printf("Failed to place world (0,0,0)\n");
        return 1;
    }
    if (!universe_place(&universe, 1, 0, 0, w1))
    {
        printf("Failed to place world (1,0,0)\n");
        return 1;
    }

    world_generate_with_type(w0, seed, WORLD_TYPE_WILDERNESS);
    world_generate_with_type(w1, seed, WORLD_TYPE_WILDERNESS);

    // Compare along the shared boundary: x=W-1 in w0 vs x=0 in w1
    uint64_t compared = 0, mismatched = 0;
    uint64_t height_compared = 0, height_mismatched = 0;

    for (uint32_t y = 0; y < H; y++)
    {
        for (uint32_t z = 0; z < D; z++)
        {
            Voxel *v0 = world_get_voxel(w0, W - 1, y, z);
            Voxel *v1 = world_get_voxel(w1, 0, y, z);
            if (v0 && v1)
            {
                compared++;
                if (v0->type != v1->type) mismatched++;
            }
        }

        int top0 = find_top_solid_z(w0, W - 1, y);
        int top1 = find_top_solid_z(w1, 0, y);
        height_compared++;
        if (top0 != top1) height_mismatched++;
    }

    double voxel_mismatch = (compared > 0) ? (double)mismatched / (double)compared : 1.0;
    double height_mismatch = (height_compared > 0) ? (double)height_mismatched / (double)height_compared : 1.0;

    printf("Boundary voxel compare: compared=%llu mismatched=%llu mismatch=%.4f\n",
           (unsigned long long)compared, (unsigned long long)mismatched, voxel_mismatch);
    printf("Boundary height compare: compared=%llu mismatched=%llu mismatch=%.4f\n",
           (unsigned long long)height_compared, (unsigned long long)height_mismatched, height_mismatch);

    // Heuristic pass criteria: fewer than 5%% voxel mismatches and fewer than 10%% height mismatches
    int pass = (voxel_mismatch < 0.05) && (height_mismatch < 0.10);
    if (pass)
    {
        printf("\u2713 World boundary continuity test PASSED\n");
        return 0;
    }
    else
    {
        printf("\u2717 World boundary continuity test FAILED\n");
        return 2;
    }
}


