// Material-worlds / first-person gameplay framerate benchmark.
//
// Optimisation tool, not a test: reports numbers against a frame budget so nested material
// templates and sparse silhouette meshes can be improved without guessing. Scenes mimic
// gameplay (lawn, bush grove, settlement fittings, home island) rather than empty grids.
//
//   make material-fp-benchmark && ./material-fp-benchmark
//   ./material-fp-benchmark --reps 5 --target-fps 60 --only frame
//   ./material-fp-benchmark --csv >> material-fp-history.csv
//
// A/B nested detail with --subvoxel 0|1 (overrides VERSE_SUBVOXEL for this process).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "constants.h"
#include "fp_renderer.h"
#include "material_worlds.h"
#include "voxel_mesh.h"
#include "world.h"

static double now_seconds(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static double g_target_fps = 60.0;
static int g_reps = 5;
static bool g_csv = false;
static bool g_fast = false;
static const char *g_only = NULL;
static int g_subvoxel = -1; // -1 env, 0/1 force

static double frame_budget_ms(void) { return 1000.0 / g_target_fps; }

static void report(const char *section, const char *what, double ms, double work,
                   const char *work_unit)
{
  if (g_csv)
  {
    printf("%s,%s,%.4f,%.0f,%s\n", section, what, ms, work, work_unit ? work_unit : "");
    return;
  }

  const double budget = frame_budget_ms();
  char verdict[32];
  if (ms <= budget)
    snprintf(verdict, sizeof(verdict), "%5.1f%% frame", ms / budget * 100.0);
  else
    snprintf(verdict, sizeof(verdict), "%5.1fx OVER", ms / budget);

  printf("  %-48s %9.3f ms  %s", what, ms, verdict);
  if (work > 0.0 && work_unit)
  {
    const double per_second = work / (ms / 1000.0);
    if (per_second >= 1e6)
      printf("   %6.2f M%s/s", per_second / 1e6, work_unit);
    else if (per_second >= 1e3)
      printf("   %6.2f K%s/s", per_second / 1e3, work_unit);
    else
      printf("   %6.2f %s/s", per_second, work_unit);
  }
  printf("\n");
}

static void section(const char *title)
{
  if (g_csv)
    return;
  printf("\n%s\n", title);
  for (const char *p = title; *p; p++)
    putchar('-');
  printf("\n");
}

static bool want(const char *name)
{
  return !g_only || strcmp(g_only, name) == 0;
}

#define MEASURE(out_ms, body)                          \
  do                                                   \
  {                                                    \
    const int reps = (g_reps > 64) ? 64 : g_reps;      \
    double best_ = 0.0;                                \
    for (int rep_ = 0; rep_ < reps; rep_++)           \
    {                                                  \
      const double t_ = now_seconds();                 \
      body;                                            \
      const double e_ = (now_seconds() - t_) * 1000.0; \
      if (rep_ == 0 || e_ < best_)                     \
        best_ = e_;                                    \
    }                                                  \
    (out_ms) = best_;                                  \
  } while (0)

// ---------------------------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------------------------

static void paint_floor(World *w, VoxelType soil, VoxelType coat, int coat_z)
{
  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
    {
      for (int z = 0; z < coat_z; z++)
        world_set_voxel(w, x, y, (uint32_t)z, soil);
      world_set_voxel(w, x, y, (uint32_t)coat_z, coat);
    }
}

static World *make_lawn_world(void)
{
  World *w = world_create(WORLD_SIZE_CUBE);
  if (!w)
    return NULL;
  paint_floor(w, VOXEL_SOIL, VOXEL_GRASS, 40);
  world_refresh_occupancy_bitfield(w);
  return w;
}

// Dense shrubbery around the camera — worst case for nested bush meshes.
static World *make_bush_grove_world(int bushes)
{
  World *w = make_lawn_world();
  if (!w)
    return NULL;
  const int cx = (int)w->width / 2;
  const int cy = (int)w->height / 2;
  const int cz = 41;
  int placed = 0;
  for (int r = 2; r < 40 && placed < bushes; r++)
  {
    for (int a = 0; a < 16 && placed < bushes; a++)
    {
      const float ang = (float)a * (6.2831853f / 16.0f) + (float)r * 0.2f;
      const int x = cx + (int)(cosf(ang) * (float)r);
      const int y = cy + (int)(sinf(ang) * (float)r);
      if (x < 1 || y < 1 || x >= (int)w->width - 1 || y >= (int)w->height - 1)
        continue;
      world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)cz, VOXEL_BUSH);
      if ((placed % 5) == 0)
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)(cz + 1), VOXEL_LEAVES);
      placed++;
    }
  }
  world_refresh_occupancy_bitfield(w);
  if (!g_csv)
    printf("  bush grove: %d sparse foliage props on a grass floor\n", placed);
  return w;
}

// Settlement-like fittings packed near the camera.
static World *make_fittings_world(void)
{
  World *w = make_lawn_world();
  if (!w)
    return NULL;
  const int cx = (int)w->width / 2;
  const int cy = (int)w->height / 2;
  const int z0 = 41;
  const VoxelType fittings[] = {
      VOXEL_DOOR,       VOXEL_DOOR_NS,   VOXEL_GLASS,      VOXEL_ROOF_TILE,
      VOXEL_ROOF_TILE_MIRROR, VOXEL_THATCH, VOXEL_STAIR,    VOXEL_STAIR_NS,
      VOXEL_CHAIR,      VOXEL_TABLE,     VOXEL_CHEST,      VOXEL_BARREL,
      VOXEL_BED,        VOXEL_CRATE,     VOXEL_GRASS_TALL, VOXEL_BUSH,
  };
  const int n = (int)(sizeof(fittings) / sizeof(fittings[0]));
  int placed = 0;
  for (int i = 0; i < n; i++)
  {
    for (int k = 0; k < 4; k++)
    {
      const int x = cx - 8 + (i % 8) * 2 + (k & 1);
      const int y = cy - 6 + (i / 8) * 3 + (k >> 1);
      world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z0, fittings[i]);
      placed++;
    }
  }
  // A short roof ridge so angled shells sit in view.
  for (int x = cx - 6; x <= cx + 6; x++)
  {
    world_set_voxel(w, (uint32_t)x, (uint32_t)(cy + 10), (uint32_t)(z0 + 2), VOXEL_ROOF_TILE);
    world_set_voxel(w, (uint32_t)x, (uint32_t)(cy + 11), (uint32_t)(z0 + 3),
                    VOXEL_ROOF_TILE_MIRROR);
  }
  world_refresh_occupancy_bitfield(w);
  if (!g_csv)
    printf("  fittings yard: %d sparse props + roof ridge\n", placed);
  return w;
}

static World *make_home_world(void)
{
  World *w = world_create(WORLD_SIZE_CUBE);
  if (!w)
    return NULL;
  world_generate_with_type(w, "mat-fp-bench-home", WORLD_TYPE_HOME);
  world_refresh_occupancy_bitfield(w);
  return w;
}

static int count_sparse_in_world(const World *w)
{
  if (!w)
    return 0;
  int n = 0;
  for (uint32_t z = 0; z < w->depth; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
      {
        const Voxel *v = world_voxel_cptr_fast(w, (int)x, (int)y, (int)z);
        if (v && world_voxel_type_has_material_gaps(v->type))
          n++;
      }
  return n;
}

static FPCamera cam_on_spawn(const World *w)
{
  FPCamera cam = {.x = (float)w->width * 0.5f,
                  .y = (float)w->height * 0.5f - 8.0f,
                  .z = 44.0f,
                  .yaw = 1.5707963f,
                  .pitch = -0.15f,
                  .fov_deg = FP_CAMERA_FOV_DEG};
  // Prefer standing height from a home-like spawn if the lawn coat differs.
  for (int z = (int)w->depth - 2; z >= 1; z--)
  {
    const Voxel *v =
        world_voxel_cptr_fast(w, (int)cam.x, (int)(cam.y + 8.0f), z);
    if (v && v->type != VOXEL_AIR)
    {
      cam.z = (float)z + 2.5f;
      break;
    }
  }
  return cam;
}

typedef struct
{
  SDL_Window *win;
  SDL_Renderer *ren;
  SDL_Texture *target;
  int w, h;
} BenchGfx;

static bool gfx_init(BenchGfx *g, int w, int h)
{
  memset(g, 0, sizeof(*g));
  g->w = w;
  g->h = h;
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    return false;
  g->win = SDL_CreateWindow("material-fp-bench", 0, 0, w, h, SDL_WINDOW_HIDDEN);
  g->ren = g->win ? SDL_CreateRenderer(g->win, -1, SDL_RENDERER_SOFTWARE) : NULL;
  g->target = g->ren ? SDL_CreateTexture(g->ren, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, w, h)
                     : NULL;
  return g->ren && g->target;
}

static void gfx_shutdown(BenchGfx *g)
{
  fp_renderer_release_renderer();
  if (g->target)
    SDL_DestroyTexture(g->target);
  if (g->ren)
    SDL_DestroyRenderer(g->ren);
  if (g->win)
    SDL_DestroyWindow(g->win);
  SDL_Quit();
}

static void warm_and_draw(BenchGfx *g, World *world, const FPCamera *cam)
{
  GameWorlds gw = {0};
  gw.home_world = world;
  SDL_SetRenderTarget(g->ren, g->target);
  fp_renderer_set_mode(FP_MODE_MESH);
  fp_renderer_enable_gpu_mesh(false);
  fp_renderer_set_lighting(false);
  fp_renderer_invalidate_cache();
  // Cold build + one warm frame so MEASURE sees steady-state raster cost.
  fp_renderer_render_neighbors(g->ren, &gw, cam, 0, 0, g->w, g->h, 0, NULL);
  fp_renderer_render_neighbors(g->ren, &gw, cam, 0, 0, g->w, g->h, 0, NULL);
}

static void measure_frame(BenchGfx *g, World *world, const FPCamera *cam, const char *label)
{
  GameWorlds gw = {0};
  gw.home_world = world;
  SDL_SetRenderTarget(g->ren, g->target);

  double ms;
  MEASURE(ms, {
    fp_renderer_clear_material_frame_stats();
    fp_renderer_render_neighbors(g->ren, &gw, cam, 0, 0, g->w, g->h, 0, NULL);
  });
  report("frame", label, ms, (double)(g->w * g->h), "px");

  const FPMaterialFrameStats *st = fp_renderer_material_frame_stats();
  if (!g_csv && st)
  {
    printf("       sparse: scan %.3f ms, draw %.3f ms, candidates %d, drawn %d, quads %d\n",
           st->sparse_scan_ms, st->sparse_draw_ms, st->sparse_candidates, st->sparse_drawn,
           st->sparse_quads);
  }
  if (g_csv && st)
  {
    report("frame", "sparse_scan", st->sparse_scan_ms, (double)st->sparse_candidates, "cell");
    report("frame", "sparse_draw", st->sparse_draw_ms, (double)st->sparse_quads, "quad");
  }
}

// ---------------------------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------------------------

static void bench_init(void)
{
  if (!want("init") && !want("materials"))
    return;
  section("Material template init (once per process)");

  material_worlds_shutdown();
  double ms;
  MEASURE(ms, {
    material_worlds_shutdown();
    (void)material_worlds_init();
  });
  report("init", "material_worlds_init (generate+bake all)", ms,
         (double)MATERIAL_TEMPLATE_COUNT, "tmpl");

  int sparse_n = 0;
  for (int k = 0; k < MATERIAL_TEMPLATE_COUNT; k++)
    if (material_template_kind_is_sparse((MaterialTemplateKind)k))
      sparse_n++;
  if (!g_csv)
    printf("  %d / %d templates are sparse (silhouette path)\n", sparse_n,
           MATERIAL_TEMPLATE_COUNT);
}

static void bench_template_meshes(void)
{
  if (!want("mesh") && !want("materials"))
    return;
  section("Sparse template mesh extraction (once per kind, then cached in FP)");

  if (!material_worlds_init())
  {
    printf("  (skipped: material_worlds_init failed)\n");
    return;
  }

  double total_ms = 0.0;
  int total_quads = 0;
  int sparse_n = 0;
  for (int k = 0; k < MATERIAL_TEMPLATE_COUNT; k++)
  {
    if (!material_template_kind_is_sparse((MaterialTemplateKind)k))
      continue;
    const MaterialTemplate *t = material_worlds_get((MaterialTemplateKind)k);
    if (!t || !t->world)
      continue;
    VoxelMesh mesh = {0};
    voxel_mesh_init(&mesh);
    double ms;
    MEASURE(ms, {
      voxel_mesh_free(&mesh);
      voxel_mesh_init(&mesh);
      voxel_mesh_build_all_faces_greedy_ex(t->world, &mesh, false);
    });
    char label[96];
    snprintf(label, sizeof(label), "mesh %s", material_template_name(t->kind));
    report("mesh", label, ms, (double)mesh.count, "quad");
    total_ms += ms;
    total_quads += mesh.count;
    sparse_n++;
    voxel_mesh_free(&mesh);
  }
  report("mesh", "sum of all sparse template meshes", total_ms, (double)total_quads, "quad");
  if (!g_csv)
    printf("  (%d sparse kinds, %d quads total — FP caches each once)\n", sparse_n, total_quads);
}

static void bench_parent_remesh(void)
{
  if (!want("remesh") && !want("materials"))
    return;
  section("Parent-world greedy remesh (skip sparse vs keep cubes)");

  World *grove = make_bush_grove_world(g_fast ? 80 : 200);
  if (!grove)
    return;
  const int sparse = count_sparse_in_world(grove);
  if (!g_csv)
    printf("  world sparse parents: %d\n", sparse);

  VoxelMesh mesh = {0};
  double ms;
  MEASURE(ms, {
    voxel_mesh_free(&mesh);
    voxel_mesh_init(&mesh);
    voxel_mesh_build_all_faces_greedy_ex(grove, &mesh, true);
  });
  report("remesh", "greedy skip_sparse=true (gameplay FP cache)", ms, (double)mesh.count, "quad");
  const int skip_quads = mesh.count;

  MEASURE(ms, {
    voxel_mesh_free(&mesh);
    voxel_mesh_init(&mesh);
    voxel_mesh_build_all_faces_greedy_ex(grove, &mesh, false);
  });
  report("remesh", "greedy skip_sparse=false (legacy cubes)", ms, (double)mesh.count, "quad");
  if (!g_csv)
    printf("  skip path saved %d parent quads vs cube emission\n", mesh.count - skip_quads);

  // Centre FP path: one full greedy + derive LOD (was two full greeds via lod_ex).
  VoxelMesh lod = {0};
  MEASURE(ms, {
    voxel_mesh_free(&mesh);
    voxel_mesh_free(&lod);
    voxel_mesh_init(&mesh);
    voxel_mesh_init(&lod);
    voxel_mesh_build_all_faces_greedy_ex(grove, &mesh, true);
    voxel_mesh_copy(&lod, &mesh);
    // Match fp_mesh_lod_from stride-2 filter cost (copy already done; filter in place).
    {
      int write = 0;
      int face_writes[6] = {0};
      for (int f = 0; f < 6; f++)
      {
        const int start = lod.face_start[f];
        const int n = lod.face_count[f];
        lod.face_start[f] = write;
        for (int i = 0; i < n; i++)
        {
          const VoxelFaceQuad *q = &lod.quads[start + i];
          if ((q->x0 % 2) != 0 || (q->y0 % 2) != 0 || (q->z0 % 2) != 0)
            continue;
          if (write != start + i)
            lod.quads[write] = *q;
          write++;
          face_writes[f]++;
        }
        lod.face_count[f] = face_writes[f];
      }
      lod.count = write;
    }
  });
  report("remesh", "full+derive LOD (centre FP path)", ms, (double)(mesh.count + lod.count),
         "quad");

  MEASURE(ms, {
    voxel_mesh_free(&lod);
    voxel_mesh_init(&lod);
    voxel_mesh_build_all_faces_greedy_lod_ex(grove, &lod, 2, true);
  });
  report("remesh", "coarse LOD stride-2 (first-presence fill)", ms, (double)lod.count, "quad");

  MEASURE(ms, {
    voxel_mesh_free(&lod);
    voxel_mesh_init(&lod);
    voxel_mesh_build_all_faces_greedy_lod_ex(grove, &lod, 4, true);
  });
  report("remesh", "coarse LOD stride-4 (busy-frame fill)", ms, (double)lod.count, "quad");

  MEASURE(ms, {
    voxel_mesh_free(&lod);
    voxel_mesh_init(&lod);
    voxel_mesh_build_all_faces_greedy_ex(grove, &lod, true);
  });
  report("remesh", "full greedy (ring-1 target quality)", ms, (double)lod.count, "quad");

  voxel_mesh_free(&lod);
  voxel_mesh_free(&mesh);
  world_destroy(grove);
}

static void bench_frames(void)
{
  if (!want("frame") && !want("fp"))
    return;
  section("FP mesh frames (256x240 gameplay panel)");

  BenchGfx gfx;
  if (!gfx_init(&gfx, 256, 240))
  {
    printf("  (skipped: no SDL software renderer — %s)\n", SDL_GetError());
    return;
  }

  fp_renderer_set_subvoxel_enabled(g_subvoxel);
  if (!g_csv)
  {
    printf("  subvoxel override=%d ( -1=env VERSE_SUBVOXEL )\n", g_subvoxel);
    printf("  panel %dx%d, target %.0f FPS = %.3f ms/frame\n\n", gfx.w, gfx.h, g_target_fps,
           frame_budget_ms());
  }

  World *lawn = make_lawn_world();
  World *grove = make_bush_grove_world(g_fast ? 60 : 160);
  World *fittings = make_fittings_world();
  World *home = g_fast ? NULL : make_home_world();

  if (lawn)
  {
    FPCamera cam = cam_on_spawn(lawn);
    warm_and_draw(&gfx, lawn, &cam);
    measure_frame(&gfx, lawn, &cam, "lawn (grass floor, no sparse props)");
  }
  if (grove)
  {
    FPCamera cam = cam_on_spawn(grove);
    warm_and_draw(&gfx, grove, &cam);
    measure_frame(&gfx, grove, &cam, "bush grove (nested shrub silhouettes)");
  }
  if (fittings)
  {
    FPCamera cam = cam_on_spawn(fittings);
    warm_and_draw(&gfx, fittings, &cam);
    measure_frame(&gfx, fittings, &cam, "fittings yard (doors/roofs/stairs/furniture)");
  }
  if (home)
  {
    FPCamera cam = cam_on_spawn(home);
    warm_and_draw(&gfx, home, &cam);
    measure_frame(&gfx, home, &cam, "home island (real WORLD_TYPE_HOME)");
  }

  // A/B inside one process when not forced by CLI: subvoxel on vs flat.
  if (g_subvoxel < 0 && grove)
  {
    FPCamera cam = cam_on_spawn(grove);
    fp_renderer_set_subvoxel_enabled(0);
    fp_renderer_invalidate_cache();
    warm_and_draw(&gfx, grove, &cam);
    measure_frame(&gfx, grove, &cam, "bush grove SUBVOXEL=0 (flat cubes)");
    fp_renderer_set_subvoxel_enabled(1);
    fp_renderer_invalidate_cache();
    warm_and_draw(&gfx, grove, &cam);
    measure_frame(&gfx, grove, &cam, "bush grove SUBVOXEL=1 (nested silhouettes)");
    fp_renderer_set_subvoxel_enabled(-1);
  }

  if (lawn)
    world_destroy(lawn);
  if (grove)
    world_destroy(grove);
  if (fittings)
    world_destroy(fittings);
  if (home)
    world_destroy(home);
  gfx_shutdown(&gfx);
}

static void usage(const char *argv0)
{
  printf("usage: %s [--target-fps N] [--reps N] [--csv] [--fast] [--only SECTION]\n", argv0);
  printf("               [--subvoxel 0|1]\n");
  printf("  --fast       smaller foliage counts; skip real home world gen\n");
  printf("  --subvoxel   force nested materials off/on (default: follow env, then A/B)\n");
  printf("  SECTION: init, mesh, remesh, frame, materials, fp\n");
}

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "--target-fps") && i + 1 < argc)
    {
      g_target_fps = atof(argv[++i]);
      if (g_target_fps <= 0.0)
        g_target_fps = 60.0;
    }
    else if (!strcmp(argv[i], "--reps") && i + 1 < argc)
    {
      g_reps = atoi(argv[++i]);
      if (g_reps < 1)
        g_reps = 1;
    }
    else if (!strcmp(argv[i], "--csv"))
      g_csv = true;
    else if (!strcmp(argv[i], "--fast"))
      g_fast = true;
    else if (!strcmp(argv[i], "--only") && i + 1 < argc)
      g_only = argv[++i];
    else if (!strcmp(argv[i], "--subvoxel") && i + 1 < argc)
    {
      g_subvoxel = atoi(argv[++i]);
      if (g_subvoxel != 0 && g_subvoxel != 1)
        g_subvoxel = -1;
    }
    else
    {
      usage(argv[0]);
      return (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) ? 0 : 1;
    }
  }

  if (g_csv)
    printf("section,measurement,ms,work,work_unit\n");
  else
  {
    printf("Material Worlds FP Framerate Benchmark\n");
    printf("======================================\n");
    printf("target %.0f FPS = %.3f ms per frame, best of %d run(s)\n", g_target_fps,
           frame_budget_ms(), g_reps);
  }

  bench_init();
  bench_template_meshes();
  bench_parent_remesh();
  bench_frames();

  material_worlds_shutdown();
  if (!g_csv)
    printf("\n");
  return 0;
}
