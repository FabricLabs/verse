#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "world.h"
#include "constants.h"
#include "wfc3d.h"
#include "universe.h"

static void ensure_worlds_dir(void)
{
  struct stat st = {0};
  if (stat("worlds", &st) == -1)
  {
    mkdir("worlds", 0755);
  }
}

static void gen_seed_hex(char *out, size_t out_len)
{
  // Produce 64 hex chars
  unsigned int t = (unsigned int)time(NULL);
  for (int i = 0; i < 32; i++)
  {
    t = t * 1103515245u + 12345u;
    sprintf(out + i * 2, "%02x", (unsigned)(t & 0xFF));
  }
  out[64] = '\0';
}

static void build_filename(char *buf, size_t buflen, const char *seed, int x, int y, int z, const char *type)
{
  snprintf(buf, buflen, "worlds/%s__%d_%d_%d__%s.world", seed, x, y, z, type);
}

static World *make_world(uint32_t w, uint32_t h, uint32_t d)
{
  return world_create(w, h, d);
}

int main(int argc, char **argv)
{
  const char *base_seed = NULL;
  int radius = 1;                      // generate a (2R+1)x(2R+1) wilderness plane
  const char *generator = "gameworld"; // {gameworld|arena}

  for (int i = 1; i < argc; i++)
  {
    if ((strcmp(argv[i], "--seed") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc)
    {
      base_seed = argv[++i];
    }
    else if ((strcmp(argv[i], "--radius") == 0 || strcmp(argv[i], "-r") == 0) && i + 1 < argc)
    {
      radius = atoi(argv[++i]);
      if (radius < 0)
        radius = 0;
    }
    else if ((strcmp(argv[i], "--generator") == 0 || strcmp(argv[i], "-g") == 0) && i + 1 < argc)
    {
      generator = argv[++i];
      if (!(strcmp(generator, "gameworld") == 0 || strcmp(generator, "arena") == 0))
      {
        printf("Unknown generator '%s'. Expected 'gameworld' or 'arena'.\n", generator);
        return 1;
      }
    }
    else
    {
      printf("Usage: %s [--seed HEX64] [--radius R] [--generator {gameworld|arena}]\n", argv[0]);
      printf("  Generator 'gameworld' (default) tileset: empty > home > cloud > wilderness > bedrock_upper > underworld > bedrock_lower.\n");
      printf("  Generator 'arena' tileset: same as 'gameworld' except origin (0,0,0) uses ARENA instead of WILDERNESS.\n");
      return 1;
    }
  }

  char seed_hex[65];
  if (!base_seed || strlen(base_seed) == 0)
  {
    gen_seed_hex(seed_hex, sizeof(seed_hex));
    base_seed = seed_hex;
  }
  else
  {
    // copy to fixed buffer to ensure string lifetime
    size_t n = strlen(base_seed);
    if (n > 64)
      n = 64;
    memcpy(seed_hex, base_seed, n);
    seed_hex[n] = '\0';
    base_seed = seed_hex;
  }

  ensure_worlds_dir();

  // Normalize universe noise seed across all worlds
  world_set_universe_noise_seed(base_seed);

  const uint32_t W = WORLD_SIZE_X, H = WORLD_SIZE_Y, D = WORLD_SIZE_Z;

  for (int gy = -radius; gy <= radius; gy++)
  {
    for (int gx = -radius; gx <= radius; gx++)
    {
      // Ground plane at z=0 (WFC decides wilderness vs arena origin)
      {
        World *w = make_world(W, H, D);
        if (!w)
          continue;
        WorldCoord coord = {gx, gy, 0};
        char *seed = world_generate_seed_for_coord(base_seed, coord);

        // Mirror universe.c selection policy (WFC tileset)
        WorldGenerationType gtype; VoxelType fill;
        UniverseGeneratorType ug = (strcmp(generator, "arena") == 0) ? UNIVERSE_GEN_ARENA : UNIVERSE_GEN_GAMEWORLD;
        universe_wfc_decide_cell(ug, gx, gy, 0, &gtype, &fill);
        if (gtype == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(w, seed ? seed : base_seed, gtype, fill);
        else world_generate_with_type(w, seed ? seed : base_seed, gtype);
        char path[512];
        build_filename(path, sizeof(path), base_seed, gx, gy, 0, (gtype == WORLD_TYPE_ARENA ? "arena" : "wilderness"));
        (void)world_save(w, path);
        // Also save by seed_id for VOXEL_WORLD references (<ID>.world)
        if (w->seed_id[0] != '\0') {
          (void)world_save_by_seed(w, w->seed_id);
        }
        if (seed)
          free(seed);
        world_destroy(w);
      }

      // Cloud at z=+1 (WFC)
      {
        World *w = make_world(W, H, D);
        if (!w)
          continue;
        WorldCoord coord = {gx, gy, 1};
        char *seed = world_generate_seed_for_coord(base_seed, coord);

        {
          WorldGenerationType gtype; VoxelType fill; universe_wfc_decide_cell(
            (strcmp(generator, "arena") == 0) ? UNIVERSE_GEN_ARENA : UNIVERSE_GEN_GAMEWORLD,
            gx, gy, 1, &gtype, &fill);
          if (gtype == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(w, seed ? seed : base_seed, gtype, fill);
          else world_generate_with_type(w, seed ? seed : base_seed, gtype);
        }
        char path[512];
        build_filename(path, sizeof(path), base_seed, gx, gy, 1, "cloud");
        (void)world_save(w, path);
        if (w->seed_id[0] != '\0') {
          (void)world_save_by_seed(w, w->seed_id);
        }
        if (seed)
          free(seed);
        world_destroy(w);
      }

      // Home at z=+2 (WFC)
      {
        World *w = make_world(W, H, D);
        if (!w)
          continue;
        WorldCoord coord = {gx, gy, 2};
        char *seed = world_generate_seed_for_coord(base_seed, coord);

        {
          WorldGenerationType gtype; VoxelType fill; universe_wfc_decide_cell(
            (strcmp(generator, "arena") == 0) ? UNIVERSE_GEN_ARENA : UNIVERSE_GEN_GAMEWORLD,
            gx, gy, 2, &gtype, &fill);
          if (gtype == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(w, seed ? seed : base_seed, gtype, fill);
          else world_generate_with_type(w, seed ? seed : base_seed, gtype);
        }
        char path[512];
        build_filename(path, sizeof(path), base_seed, gx, gy, 2, "home");
        (void)world_save(w, path);
        if (w->seed_id[0] != '\0') {
          (void)world_save_by_seed(w, w->seed_id);
        }
        if (seed)
          free(seed);
        world_destroy(w);
      }

      // Empty at z=+3 (WFC -> SOLID AIR)
      {
        World *w = make_world(W, H, D);
        if (!w)
          continue;
        WorldCoord coord = {gx, gy, 3};
        char *seed = world_generate_seed_for_coord(base_seed, coord);

        {
          WorldGenerationType gtype; VoxelType fill; universe_wfc_decide_cell(
            (strcmp(generator, "arena") == 0) ? UNIVERSE_GEN_ARENA : UNIVERSE_GEN_GAMEWORLD,
            gx, gy, 3, &gtype, &fill);
          if (gtype == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(w, seed ? seed : base_seed, gtype, fill);
          else world_generate_with_type(w, seed ? seed : base_seed, gtype);
        }
        char path[512];
        build_filename(path, sizeof(path), base_seed, gx, gy, 3, "empty");
        (void)world_save(w, path);
        if (w->seed_id[0] != '\0') {
          (void)world_save_by_seed(w, w->seed_id);
        }
        if (seed)
          free(seed);
        world_destroy(w);
      }

      // Bedrock upper at z=-1 (WFC -> SOLID BEDROCK)
      {
        World *w = make_world(W, H, D);
        if (!w)
          continue;
        WorldCoord coord = {gx, gy, -1};
        char *seed = world_generate_seed_for_coord(base_seed, coord);

        {
          WorldGenerationType gtype; VoxelType fill; universe_wfc_decide_cell(
            (strcmp(generator, "arena") == 0) ? UNIVERSE_GEN_ARENA : UNIVERSE_GEN_GAMEWORLD,
            gx, gy, -1, &gtype, &fill);
          if (gtype == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(w, seed ? seed : base_seed, gtype, fill);
          else world_generate_with_type(w, seed ? seed : base_seed, gtype);
        }
        char path[512];
        build_filename(path, sizeof(path), base_seed, gx, gy, -1, "bedrock_upper");
        (void)world_save(w, path);
        if (w->seed_id[0] != '\0') {
          (void)world_save_by_seed(w, w->seed_id);
        }
        if (seed)
          free(seed);
        world_destroy(w);
      }

      // Underworld at z=-2 (WFC)
      {
        World *w = make_world(W, H, D);
        if (!w)
          continue;
        WorldCoord coord = {gx, gy, -2};
        char *seed = world_generate_seed_for_coord(base_seed, coord);

        {
          WorldGenerationType gtype; VoxelType fill; universe_wfc_decide_cell(
            (strcmp(generator, "arena") == 0) ? UNIVERSE_GEN_ARENA : UNIVERSE_GEN_GAMEWORLD,
            gx, gy, -2, &gtype, &fill);
          if (gtype == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(w, seed ? seed : base_seed, gtype, fill);
          else world_generate_with_type(w, seed ? seed : base_seed, gtype);
        }
        char path[512];
        build_filename(path, sizeof(path), base_seed, gx, gy, -2, "underworld");
        (void)world_save(w, path);
        if (w->seed_id[0] != '\0') {
          (void)world_save_by_seed(w, w->seed_id);
        }
        if (seed)
          free(seed);
        world_destroy(w);
      }

      // Bedrock lower at z=-3 (WFC -> SOLID BEDROCK)
      {
        World *w = make_world(W, H, D);
        if (!w)
          continue;
        WorldCoord coord = {gx, gy, -3};
        char *seed = world_generate_seed_for_coord(base_seed, coord);

        {
          WorldGenerationType gtype; VoxelType fill; universe_wfc_decide_cell(
            (strcmp(generator, "arena") == 0) ? UNIVERSE_GEN_ARENA : UNIVERSE_GEN_GAMEWORLD,
            gx, gy, -3, &gtype, &fill);
          if (gtype == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(w, seed ? seed : base_seed, gtype, fill);
          else world_generate_with_type(w, seed ? seed : base_seed, gtype);
        }
        char path[512];
        build_filename(path, sizeof(path), base_seed, gx, gy, -3, "bedrock_lower");
        (void)world_save(w, path);
        if (w->seed_id[0] != '\0') {
          (void)world_save_by_seed(w, w->seed_id);
        }
        if (seed)
          free(seed);
        world_destroy(w);
      }
    }
  }

  // Build a meta world composed purely of VOXEL_WORLD that indexes the generated tiles
  // Dimensions: X = 2R+1, Y = 2R+1, Z = 7 layers ([-3..+3] mapped to [0..6])
  const uint32_t MW = (uint32_t)(radius * 2 + 1);
  const uint32_t MH = (uint32_t)(radius * 2 + 1);
  const uint32_t MD = 7u;
  World *meta = make_world(MW, MH, MD);
  if (meta)
  {
    // Clear to AIR
    for (uint32_t z = 0; z < MD; z++)
      for (uint32_t y = 0; y < MH; y++)
        for (uint32_t x = 0; x < MW; x++)
          world_set_voxel(meta, x, y, z, VOXEL_AIR);

    // Fill VOXEL_WORLD where we generated tiles
    for (int gy = -radius; gy <= radius; gy++)
    {
      for (int gx = -radius; gx <= radius; gx++)
      {
        uint32_t mx = (uint32_t)(gx + radius);
        uint32_t my = (uint32_t)(gy + radius);
        // For each universe layer -3..+3
        for (int gz = -3; gz <= 3; gz++)
        {
          uint32_t mz = (uint32_t)(gz + 3);
          world_set_voxel(meta, mx, my, mz, VOXEL_WORLD);
        }
      }
    }

    // Name and save: worlds/<TYPE>_DEFAULT.world
    char type_upper[32];
    size_t glen = strlen(generator);
    if (glen > sizeof(type_upper) - 1) glen = sizeof(type_upper) - 1;
    for (size_t i = 0; i < glen; i++) type_upper[i] = (char)toupper((unsigned char)generator[i]);
    type_upper[glen] = '\0';

    char meta_path[128];
    snprintf(meta_path, sizeof(meta_path), "worlds/%s_DEFAULT.world", type_upper);
    (void)world_save(meta, meta_path);
    world_destroy(meta);
  }

  printf("Generated universe with base seed %.12s... radius=%d generator=%s\n", base_seed, radius, generator);
  return 0;
}
