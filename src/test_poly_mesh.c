#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "poly_mesh.h"

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const char *what)
{
  g_checks++;
  if (condition)
    printf("  ok   %s\n", what);
  else
  {
    printf("  FAIL %s\n", what);
    g_failures++;
  }
}

int main(void)
{
  check(poly_mesh_init("models/poly"), "poly_mesh_init succeeds");
  check(poly_mesh_init("models/poly"), "poly_mesh_init is idempotent");

  const PolyMesh *golem = poly_mesh_get("goleling");
  const PolyMesh *pigeon = poly_mesh_get("pigeon");
  check(golem && golem->loaded && golem->vertex_count > 200, "goleling mesh loaded");
  check(pigeon && pigeon->loaded && pigeon->vertex_count > 200, "pigeon mesh loaded");
  check(poly_mesh_get("nope") == NULL, "unknown mesh name is NULL");
  if (golem)
  {
    check(poly_mesh_has_clip(golem, "Flying_Idle"), "goleling has Flying_Idle");
    check(poly_mesh_has_clip(golem, "Punch"), "goleling has Punch");
    float *xyz = (float *)malloc((size_t)golem->vertex_count * 3 * sizeof(float));
    check(xyz && poly_mesh_sample(golem, "Flying_Idle", 0.0f, xyz), "sample rest pose");
    if (xyz)
    {
      float min_x = xyz[0], max_x = xyz[0];
      float min_y = xyz[1], max_y = xyz[1];
      float min_z = xyz[2];
      for (uint32_t v = 1; v < golem->vertex_count; v++)
      {
        float x = xyz[v * 3], y = xyz[v * 3 + 1], z = xyz[v * 3 + 2];
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
        if (z < min_z) min_z = z;
      }
      float mid_x = 0.5f * (min_x + max_x);
      float mid_y = 0.5f * (min_y + max_y);
      check(fabsf(mid_x) < 0.15f && fabsf(mid_y) < 0.15f,
            "goleling rest pose is centred on the footprint");
      check(fabsf(min_z) < 0.15f, "goleling sole sits near z=0");
    }
    free(xyz);
  }

  const PolyMesh *sheep = poly_mesh_get("sheep");
  const PolyMesh *chick = poly_mesh_get("chick");
  const PolyMesh *bat = poly_mesh_get("bat");
  check(sheep && sheep->loaded && sheep->vertex_count > 100, "sheep mesh loaded");
  check(chick && chick->loaded && chick->vertex_count > 100, "chick mesh loaded");
  check(bat && bat->loaded && bat->vertex_count > 100, "bat mesh loaded");
  if (sheep)
  {
    check(poly_mesh_has_clip(sheep, "Idle"), "sheep has Idle");
    check(poly_mesh_has_clip(sheep, "Walk"), "sheep has Walk");
    check(poly_mesh_has_clip(sheep, "Death"), "sheep has Death");
    // Regression: a broken skinned bake left Walk with Y≈3 while Idle stayed Y≈0.8
    // (triangles stretched across the actor). Compact locomotion AABBs are required.
    float *idle_xyz = (float *)malloc((size_t)sheep->vertex_count * 3u * sizeof(float));
    float *walk_xyz = (float *)malloc((size_t)sheep->vertex_count * 3u * sizeof(float));
    check(idle_xyz && walk_xyz && poly_mesh_sample(sheep, "Idle", 0.0f, idle_xyz) &&
              poly_mesh_sample(sheep, "Walk", 0.0f, walk_xyz),
          "sample sheep Idle and Walk");
    if (idle_xyz && walk_xyz)
    {
      float idle_min[3], idle_max[3], walk_min[3], walk_max[3];
      for (int a = 0; a < 3; a++)
      {
        idle_min[a] = idle_max[a] = idle_xyz[a];
        walk_min[a] = walk_max[a] = walk_xyz[a];
      }
      for (uint32_t v = 1; v < sheep->vertex_count; v++)
      {
        for (int a = 0; a < 3; a++)
        {
          float iv = idle_xyz[v * 3 + a], wv = walk_xyz[v * 3 + a];
          if (iv < idle_min[a]) idle_min[a] = iv;
          if (iv > idle_max[a]) idle_max[a] = iv;
          if (wv < walk_min[a]) walk_min[a] = wv;
          if (wv > walk_max[a]) walk_max[a] = wv;
        }
      }
      float idle_diag = 0.0f, walk_diag = 0.0f;
      for (int a = 0; a < 3; a++)
      {
        float id = idle_max[a] - idle_min[a], wd = walk_max[a] - walk_min[a];
        idle_diag += id * id;
        walk_diag += wd * wd;
      }
      idle_diag = sqrtf(idle_diag);
      walk_diag = sqrtf(walk_diag);
      check(idle_diag > 0.5f && walk_diag < idle_diag * 1.75f,
            "sheep Walk pose stays compact vs Idle (bind pose intact)");
    }
    free(idle_xyz);
    free(walk_xyz);
  }
  if (chick)
  {
    check(poly_mesh_has_clip(chick, "Walk"), "chick has Walk");
    check(poly_mesh_has_clip(chick, "Run"), "chick has Run");
    // Kenney ships a 2-frame static Idle; the baker drops it so AI uses Walk.
    check(!poly_mesh_has_clip(chick, "Idle"), "chick static Idle was dropped");
  }
  if (bat)
  {
    check(poly_mesh_has_clip(bat, "Flying"), "bat has Flying");
    check(poly_mesh_has_clip(bat, "Idle"), "bat has Idle");
    float *xyz = (float *)malloc((size_t)bat->vertex_count * 3u * sizeof(float));
    check(xyz && poly_mesh_sample(bat, "Idle", 0.0f, xyz), "sample bat Idle");
    if (xyz)
    {
      float min_x = xyz[0], max_x = xyz[0], min_y = xyz[1], max_y = xyz[1], min_z = xyz[2];
      for (uint32_t v = 1; v < bat->vertex_count; v++)
      {
        float x = xyz[v * 3], y = xyz[v * 3 + 1], z = xyz[v * 3 + 2];
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
        if (z < min_z) min_z = z;
      }
      float span_x = max_x - min_x, span_y = max_y - min_y;
      check(span_x < 4.5f && span_y < 4.5f, "bat footprint is wingspan-capped (not authoring scale)");
      check(fabsf(min_z) < 0.2f, "bat sole sits near z=0");
    }
    free(xyz);
    // Flying must actually flap — a prior bake left every clip as a static T-pose.
    float *a = (float *)malloc((size_t)bat->vertex_count * 3u * sizeof(float));
    float *b = (float *)malloc((size_t)bat->vertex_count * 3u * sizeof(float));
    check(a && b && poly_mesh_sample(bat, "Flying", 0.0f, a) &&
              poly_mesh_sample(bat, "Flying", 0.35f, b),
          "sample bat Flying at two times");
    if (a && b)
    {
      double delta = 0.0;
      for (uint32_t i = 0; i < bat->vertex_count * 3u; i++)
        delta += fabsf(a[i] - b[i]);
      check(delta / (double)bat->vertex_count > 1e-3, "bat Flying deforms between frames");
    }
    free(a);
    free(b);
  }

  // Kenney Cube Pets remainder + Gobkit camp minions
  const PolyMesh *cow = poly_mesh_get("cow");
  const PolyMesh *parrot = poly_mesh_get("parrot");
  const PolyMesh *minion = poly_mesh_get("minion-a01");
  check(cow && cow->loaded && cow->vertex_count > 100, "cow mesh loaded");
  check(parrot && parrot->loaded && parrot->vertex_count > 100, "parrot mesh loaded");
  check(minion && minion->loaded && minion->vertex_count > 50, "minion-a01 mesh loaded");
  if (cow)
  {
    check(poly_mesh_has_clip(cow, "Walk"), "cow has Walk");
    check(poly_mesh_has_clip(cow, "Run"), "cow has Run");
  }
  if (minion)
  {
    check(poly_mesh_has_clip(minion, "Idle"), "minion has Idle");
    check(poly_mesh_has_clip(minion, "Attack"), "minion has Attack");
    check(poly_mesh_has_clip(minion, "Death"), "minion has Death");
  }

  const PolyMesh *flesh = poly_mesh_get("flesh_walker");
  check(flesh && flesh->loaded && flesh->vertex_count > 200, "flesh_walker mesh loaded");
  if (flesh)
  {
    check(poly_mesh_has_clip(flesh, "Idle"), "flesh_walker has Idle");
    check(poly_mesh_has_clip(flesh, "Walk"), "flesh_walker has Walk");
    check(poly_mesh_has_clip(flesh, "Run"), "flesh_walker has Run");
    float *a = (float *)malloc((size_t)flesh->vertex_count * 3u * sizeof(float));
    float *b = (float *)malloc((size_t)flesh->vertex_count * 3u * sizeof(float));
    check(a && b && poly_mesh_sample(flesh, "Walk", 0.0f, a) &&
              poly_mesh_sample(flesh, "Walk", 1.2f, b),
          "sample flesh_walker Walk at two times");
    if (a && b)
    {
      double delta = 0.0;
      for (uint32_t i = 0; i < flesh->vertex_count * 3u; i++)
        delta += fabsf(a[i] - b[i]);
      check(delta / (double)flesh->vertex_count > 1e-3,
            "flesh_walker Walk deforms (not a stale T-pose bake)");
    }
    free(a);
    free(b);
  }

  poly_mesh_shutdown();
  check(poly_mesh_init("models/poly"), "re-init after shutdown");
  check(poly_mesh_get("goleling") != NULL, "get after re-init loads on demand");
  poly_mesh_shutdown();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
