// Tests for the sub-voxel material template worlds.
//
// The properties worth pinning down are the ones that make a template useful as a texture: that it
// is reproducible, that its faces actually vary (a template that bakes to a flat colour is a more
// expensive way to draw what we already had), and that each material varies in the way that makes
// it recognisable — bark grooved along the trunk, sandstone banded across it, leaves full of gaps.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "material_worlds.h"
#include "voxel_mesh.h"
#include "world.h"

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const char *what)
{
  g_checks++;
  if (condition)
  {
    printf("  ok   %s\n", what);
  }
  else
  {
    printf("  FAIL %s\n", what);
    g_failures++;
  }
}

// ---------------------------------------------------------------------------------------------
// Helpers over a baked face
// ---------------------------------------------------------------------------------------------

static uint8_t texel_a(uint32_t t) { return (uint8_t)(t >> 24); }
static uint8_t texel_r(uint32_t t) { return (uint8_t)(t >> 16); }
static uint8_t texel_g(uint32_t t) { return (uint8_t)(t >> 8); }
static uint8_t texel_b(uint32_t t) { return (uint8_t)t; }

// Green-dominant, which for these palettes means a blade of grass or a leaf rather than earth.
static bool is_greenish(uint32_t t)
{
  return texel_g(t) > texel_r(t) && texel_g(t) > texel_b(t);
}

static int count_distinct_colours(const uint32_t *texels, int count)
{
  // The palettes here are tiny, so a linear scan over what we have seen is fine and avoids
  // pulling in a hash table for a test.
  uint32_t seen[256];
  int seen_count = 0;
  for (int i = 0; i < count; i++)
  {
    bool found = false;
    for (int j = 0; j < seen_count; j++)
      if (seen[j] == texels[i]) { found = true; break; }
    if (!found && seen_count < (int)(sizeof(seen) / sizeof(seen[0])))
      seen[seen_count++] = texels[i];
  }
  return seen_count;
}

static int count_opaque(const uint32_t *texels, int count)
{
  int n = 0;
  for (int i = 0; i < count; i++)
    if (texel_a(texels[i]) != 0) n++;
  return n;
}

// Mean absolute difference between horizontally adjacent texels, and between vertically adjacent
// ones. The ratio tells us which way a material's grain runs: bark should differ far more across
// its ridges than along them.
static void directional_variation(const uint32_t *texels, double *across_u, double *across_v)
{
  double du = 0.0, dv = 0.0;
  int nu = 0, nv = 0;
  for (int v = 0; v < MATERIAL_FACE_SIZE; v++)
    for (int u = 0; u < MATERIAL_FACE_SIZE; u++)
    {
      const uint32_t c = texels[v * MATERIAL_FACE_SIZE + u];
      if (u + 1 < MATERIAL_FACE_SIZE)
      {
        const uint32_t n = texels[v * MATERIAL_FACE_SIZE + (u + 1)];
        du += fabs((double)texel_r(c) - texel_r(n)) + fabs((double)texel_g(c) - texel_g(n)) +
              fabs((double)texel_b(c) - texel_b(n));
        nu++;
      }
      if (v + 1 < MATERIAL_FACE_SIZE)
      {
        const uint32_t n = texels[(v + 1) * MATERIAL_FACE_SIZE + u];
        dv += fabs((double)texel_r(c) - texel_r(n)) + fabs((double)texel_g(c) - texel_g(n)) +
              fabs((double)texel_b(c) - texel_b(n));
        nv++;
      }
    }
  *across_u = nu ? du / nu : 0.0;
  *across_v = nv ? dv / nv : 0.0;
}

// Green texels with no green 4-neighbour. Independent per-column blades produce a field of these
// on the top face; a lawn or tuft is connected patches, so isolated blades are the artifact.
static int count_isolated_green(const uint32_t *texels)
{
  int isolated = 0;
  for (int v = 0; v < MATERIAL_FACE_SIZE; v++)
    for (int u = 0; u < MATERIAL_FACE_SIZE; u++)
    {
      const uint32_t t = texels[v * MATERIAL_FACE_SIZE + u];
      if (texel_a(t) == 0 || !is_greenish(t))
        continue;
      int n = 0;
      const int u0 = (u + MATERIAL_FACE_SIZE - 1) % MATERIAL_FACE_SIZE;
      const int u1 = (u + 1) % MATERIAL_FACE_SIZE;
      const int v0 = (v + MATERIAL_FACE_SIZE - 1) % MATERIAL_FACE_SIZE;
      const int v1 = (v + 1) % MATERIAL_FACE_SIZE;
      const uint32_t l = texels[v * MATERIAL_FACE_SIZE + u0];
      const uint32_t r = texels[v * MATERIAL_FACE_SIZE + u1];
      const uint32_t a = texels[v0 * MATERIAL_FACE_SIZE + u];
      const uint32_t b = texels[v1 * MATERIAL_FACE_SIZE + u];
      if (texel_a(l) && is_greenish(l)) n++;
      if (texel_a(r) && is_greenish(r)) n++;
      if (texel_a(a) && is_greenish(a)) n++;
      if (texel_a(b) && is_greenish(b)) n++;
      if (n == 0)
        isolated++;
    }
  return isolated;
}

// ---------------------------------------------------------------------------------------------

static void test_registry(void)
{
  printf("\nRegistry\n--------\n");

  check(material_worlds_init(), "material_worlds_init succeeds");
  check(material_worlds_init(), "material_worlds_init is idempotent");

  bool all_present = true, all_baked = true, all_sized = true;
  for (int i = 0; i < MATERIAL_TEMPLATE_COUNT; i++)
  {
    const MaterialTemplate *t = material_worlds_get((MaterialTemplateKind)i);
    if (!t || !t->world) { all_present = false; continue; }
    if (!t->baked) all_baked = false;
    if (t->world->width != MATERIAL_WORLD_SIZE || t->world->height != MATERIAL_WORLD_SIZE ||
        t->world->depth != MATERIAL_WORLD_SIZE)
      all_sized = false;
  }
  check(all_present, "every template kind has a generated world");
  check(all_baked, "every template has baked faces");
  check(all_sized, "every template world is 32x32x32");

  check(material_worlds_get((MaterialTemplateKind)-1) == NULL, "out-of-range kind returns NULL");
  check(material_worlds_get(MATERIAL_TEMPLATE_COUNT) == NULL, "kind == COUNT returns NULL");
}

static void test_voxel_mapping(void)
{
  printf("\nParent voxel to template mapping\n--------------------------------\n");

  const MaterialTemplate *grass = material_worlds_for_voxel(VOXEL_GRASS);
  check(grass && grass->kind == MATERIAL_TEMPLATE_GRASS, "VOXEL_GRASS maps to the grass template");

  const MaterialTemplate *moss = material_worlds_for_voxel(VOXEL_GRASS_MOSS);
  check(moss && moss->kind == MATERIAL_TEMPLATE_GRASS,
        "grass variants share the one grass template");

  const MaterialTemplate *tall = material_worlds_for_voxel(VOXEL_GRASS_TALL);
  check(tall && tall->kind == MATERIAL_TEMPLATE_GRASS_TALL,
        "VOXEL_GRASS_TALL maps to the tall-grass template");

  const MaterialTemplate *bush = material_worlds_for_voxel(VOXEL_BUSH);
  const MaterialTemplate *fern = material_worlds_for_voxel(VOXEL_BUSH_FERN);
  check(bush && bush->kind == MATERIAL_TEMPLATE_BUSH, "VOXEL_BUSH maps to the bush template");
  check(bush == fern, "every bush species shares the one bush template");

  const MaterialTemplate *leaves = material_worlds_for_voxel(VOXEL_LEAVES_MAPLE);
  check(leaves && leaves->kind == MATERIAL_TEMPLATE_LEAVES,
        "leaf species map to the leaves template");

  const MaterialTemplate *oak = material_worlds_for_voxel(VOXEL_WOOD_OAK);
  const MaterialTemplate *pine = material_worlds_for_voxel(VOXEL_WOOD_PINE);
  check(oak && oak->kind == MATERIAL_TEMPLATE_WOOD_BARK, "VOXEL_WOOD_OAK maps to bark");
  check(oak == pine, "every wood species shares the one bark template");

  const MaterialTemplate *granite = material_worlds_for_voxel(VOXEL_STONE_GRANITE);
  check(granite && granite->kind == MATERIAL_TEMPLATE_GRANITE, "granite maps to the granite template");

  const MaterialTemplate *sandstone = material_worlds_for_voxel(VOXEL_STONE_SANDSTONE);
  check(sandstone && sandstone->kind == MATERIAL_TEMPLATE_SANDSTONE,
        "sandstone maps to the sandstone template");

  const MaterialTemplate *stone = material_worlds_for_voxel(VOXEL_STONE);
  check(stone && stone->kind == MATERIAL_TEMPLATE_STONE, "generic stone maps to the stone template");

  check(material_worlds_for_voxel(VOXEL_AIR) == NULL, "air has no template");
  check(material_worlds_for_voxel(VOXEL_WATER) == NULL,
        "fluids stay intentionally flat (no nested material template)");

  const MaterialTemplate *thatch = material_worlds_for_voxel(VOXEL_THATCH);
  check(thatch && thatch->kind == MATERIAL_TEMPLATE_THATCH, "VOXEL_THATCH maps to thatch");
  const MaterialTemplate *thatch_m = material_worlds_for_voxel(VOXEL_THATCH_MIRROR);
  check(thatch_m && thatch_m->kind == MATERIAL_TEMPLATE_THATCH_MIRROR,
        "VOXEL_THATCH_MIRROR maps to mirrored thatch for the far roof half");

  const MaterialTemplate *roof = material_worlds_for_voxel(VOXEL_ROOF_TILE);
  check(roof && roof->kind == MATERIAL_TEMPLATE_ROOF_TILE, "VOXEL_ROOF_TILE maps to roof tile");
  check(material_worlds_for_voxel(VOXEL_ROOF_TILE_MIRROR)->kind == MATERIAL_TEMPLATE_ROOF_TILE_MIRROR,
        "VOXEL_ROOF_TILE_MIRROR maps to mirrored roof tile");

  const MaterialTemplate *door = material_worlds_for_voxel(VOXEL_DOOR);
  check(door && door->kind == MATERIAL_TEMPLATE_DOOR, "VOXEL_DOOR maps to the door template");
  check(material_worlds_for_voxel(VOXEL_DOOR_NS)->kind == MATERIAL_TEMPLATE_DOOR_NS,
        "VOXEL_DOOR_NS maps to the N/S door template");

  const MaterialTemplate *glass = material_worlds_for_voxel(VOXEL_GLASS);
  check(glass && glass->kind == MATERIAL_TEMPLATE_GLASS, "VOXEL_GLASS maps to the glass pane");
  check(material_worlds_for_voxel(VOXEL_GLASS_BLUE) == glass,
        "stained glass shares the thin-pane template");
  check(material_worlds_for_voxel(VOXEL_GLASS_NS)->kind == MATERIAL_TEMPLATE_GLASS_NS,
        "VOXEL_GLASS_NS maps to the N/S glass template");
  check(material_worlds_for_voxel(VOXEL_STRAW)->kind == MATERIAL_TEMPLATE_STRAW,
        "VOXEL_STRAW maps to the loose-straw bedding template");
  check(material_worlds_for_voxel(VOXEL_FUNGUS)->kind == MATERIAL_TEMPLATE_FUNGUS,
        "VOXEL_FUNGUS maps to the fungus template");
  check(material_worlds_for_voxel(VOXEL_GRAVEL)->kind == MATERIAL_TEMPLATE_GRAVEL,
        "plain VOXEL_GRAVEL maps to gravel");
  check(material_worlds_for_voxel(VOXEL_SAND_BASALT)->kind == MATERIAL_TEMPLATE_SAND,
        "sand species share the sand template");
  check(material_worlds_for_voxel(VOXEL_BRICK)->kind == MATERIAL_TEMPLATE_BRICK,
        "VOXEL_BRICK maps to brick");
  check(material_worlds_for_voxel(VOXEL_PLANK)->kind == MATERIAL_TEMPLATE_PLANK,
        "VOXEL_PLANK maps to milled plank (not bark)");
  check(material_worlds_for_voxel(VOXEL_WOOL_RED)->kind == MATERIAL_TEMPLATE_WOOL,
        "wool dyes share the wool template");
  check(material_worlds_for_voxel(VOXEL_IRON)->kind == MATERIAL_TEMPLATE_METAL,
        "refined metals share the metal template");
  check(material_worlds_for_voxel(VOXEL_ORE_GOLD)->kind == MATERIAL_TEMPLATE_ORE,
        "ore species share the ore template");
  check(world_voxel_type_has_material_gaps(VOXEL_FUNGUS) &&
            world_voxel_type_has_material_gaps(VOXEL_FEATHER),
        "fungus and feather keep sparse bake gaps");
  check(world_voxel_type_has_material_gaps(VOXEL_THATCH) &&
            world_voxel_type_has_material_gaps(VOXEL_DOOR) &&
            world_voxel_type_has_material_gaps(VOXEL_CHAIR),
        "settlement fittings still report material gaps");
  check(material_worlds_for_voxel(VOXEL_STAIR)->kind == MATERIAL_TEMPLATE_STAIR,
        "VOXEL_STAIR maps to the stair template");
  check(material_worlds_for_voxel(VOXEL_CHAIR)->kind == MATERIAL_TEMPLATE_CHAIR,
        "VOXEL_CHAIR maps to the chair template");
  check(material_worlds_for_voxel(VOXEL_TABLE)->kind == MATERIAL_TEMPLATE_TABLE,
        "VOXEL_TABLE maps to the table template");
  check(material_worlds_for_voxel(VOXEL_CHEST)->kind == MATERIAL_TEMPLATE_CHEST,
        "VOXEL_CHEST maps to the chest template");
}

static void test_determinism(void)
{
  printf("\nDeterminism\n-----------\n");

  bool identical_worlds = true, identical_bakes = true;
  for (int i = 0; i < MATERIAL_TEMPLATE_COUNT; i++)
  {
    World *a = material_world_generate((MaterialTemplateKind)i);
    World *b = material_world_generate((MaterialTemplateKind)i);
    if (!a || !b) { identical_worlds = false; world_destroy(a); world_destroy(b); continue; }

    const size_t total = (size_t)a->width * a->height * a->depth;
    for (size_t k = 0; k < total; k++)
      if (a->voxels[k].type != b->voxels[k].type) { identical_worlds = false; break; }

    MaterialFaceBake ba, bb;
    if (material_world_bake_faces(a, &ba) && material_world_bake_faces(b, &bb))
    {
      if (memcmp(&ba, &bb, sizeof(ba)) != 0) identical_bakes = false;
    }
    else
    {
      identical_bakes = false;
    }

    world_destroy(a);
    world_destroy(b);
  }
  check(identical_worlds, "regenerating a template gives identical sub-voxels");
  check(identical_bakes, "rebaking a template gives identical texels");
}

static void test_faces_carry_detail(void)
{
  printf("\nBaked faces carry detail\n------------------------\n");

  const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
  bool none_flat = true;
  bool tops_varied = true;
  bool tops_opaque = true;
  int worst_colours = 1 << 30;
  char worst_where[64] = "";
  int worst_top_colours = 1 << 30;
  char worst_top_where[64] = "";

  printf("  %-12s %-6s %8s %8s\n", "material", "face", "colours", "opaque%");
  for (int i = 0; i < MATERIAL_TEMPLATE_COUNT; i++)
  {
    const MaterialTemplate *t = material_worlds_get((MaterialTemplateKind)i);
    if (!t) { none_flat = false; continue; }

    for (int f = 0; f < MATERIAL_FACE_COUNT; f++)
    {
      const uint32_t *texels = t->bake.texels[f];
      const int distinct = count_distinct_colours(texels, total);
      const int opaque = count_opaque(texels, total);

      printf("  %-12s %-6s %8d %7.0f%%\n", material_template_name(t->kind),
             material_face_name((MaterialFace)f), distinct,
             100.0 * (double)opaque / (double)total);

      if (distinct < worst_colours)
      {
        worst_colours = distinct;
        snprintf(worst_where, sizeof(worst_where), "%s %s", material_template_name(t->kind),
                 material_face_name((MaterialFace)f));
      }

      // A face that bakes to one colour is a flat diamond with extra steps. Sparse materials may
      // leave whole faces almost empty (edge-on panes); only judge faces that actually show.
      const bool sparse_material =
          (t->kind == MATERIAL_TEMPLATE_LEAVES || t->kind == MATERIAL_TEMPLATE_GRASS_TALL ||
           t->kind == MATERIAL_TEMPLATE_BUSH ||
           t->kind == MATERIAL_TEMPLATE_THATCH || t->kind == MATERIAL_TEMPLATE_THATCH_MIRROR ||
           t->kind == MATERIAL_TEMPLATE_ROOF_TILE ||
           t->kind == MATERIAL_TEMPLATE_ROOF_TILE_MIRROR || t->kind == MATERIAL_TEMPLATE_DOOR ||
           t->kind == MATERIAL_TEMPLATE_DOOR_NS || t->kind == MATERIAL_TEMPLATE_GLASS ||
           t->kind == MATERIAL_TEMPLATE_GLASS_NS || t->kind == MATERIAL_TEMPLATE_CRATE ||
           t->kind == MATERIAL_TEMPLATE_BARREL || t->kind == MATERIAL_TEMPLATE_BED ||
           t->kind == MATERIAL_TEMPLATE_STAIR || t->kind == MATERIAL_TEMPLATE_STAIR_NS ||
           t->kind == MATERIAL_TEMPLATE_CHAIR || t->kind == MATERIAL_TEMPLATE_TABLE ||
           t->kind == MATERIAL_TEMPLATE_CHEST || t->kind == MATERIAL_TEMPLATE_FENCE ||
           t->kind == MATERIAL_TEMPLATE_FENCE_NS || t->kind == MATERIAL_TEMPLATE_FENCE_WATTLE ||
           t->kind == MATERIAL_TEMPLATE_FENCE_IRON || t->kind == MATERIAL_TEMPLATE_PARAPET ||
           t->kind == MATERIAL_TEMPLATE_FUNGUS ||
           t->kind == MATERIAL_TEMPLATE_FEATHER);
      if (opaque > total / 8 && distinct < 2)
        none_flat = false;

      // The top is the face the isometric camera devotes the most pixels to, and it is a weathered
      // surface: pitted stone, rippled sand, blades over soil. So it gets held to more than the
      // bare minimum. An underside is a flat cut through the material and honestly has only the
      // colours the material itself offers at that depth — two, for sand and sandstone — which is
      // speckle rather than a flat tile and is all it should be.
      // Sparse roof/pane tops are mostly open air above the shell; skip the solid-top colour bar.
      if (f == MATERIAL_FACE_TOP && !sparse_material)
      {
        if (distinct < 3)
          tops_varied = false;
        if (distinct < worst_top_colours)
        {
          worst_top_colours = distinct;
          snprintf(worst_top_where, sizeof(worst_top_where), "%s",
                   material_template_name(t->kind));
        }
      }

      // Ground materials must be opaque on every face. Sparse surface materials keep transparent
      // gaps — angled roofs, door/glass panes, foliage.
      if (!sparse_material && opaque < total)
        tops_opaque = false;
    }
  }

  printf("  (least varied: %s, %d colours; least varied top: %s, %d)\n", worst_where,
         worst_colours, worst_top_where, worst_top_colours);
  check(none_flat, "no baked face is a single flat colour");
  check(tops_varied, "every solid top face has at least 3 distinct colours");
  check(tops_opaque, "every solid material's faces are fully opaque");

  const MaterialTemplate *leaves = material_worlds_get(MATERIAL_TEMPLATE_LEAVES);
  const int leaf_opaque =
      leaves ? count_opaque(leaves->bake.texels[MATERIAL_FACE_TOP], total) : -1;
  check(leaf_opaque > 0 && leaf_opaque < total,
        "leaves bake partly transparent, so a canopy shows sky through it");

  const MaterialTemplate *bush_tmpl = material_worlds_get(MATERIAL_TEMPLATE_BUSH);
  const int bush_opaque =
      bush_tmpl ? count_opaque(bush_tmpl->bake.texels[MATERIAL_FACE_TOP], total) : -1;
  check(bush_opaque > total / 10 && bush_opaque < total,
        "bushes bake a patchy canopy — gaps show ground, cover still reads as a shrub");

  // Tall grass, where the transparency has to follow the height of the blades rather than just be
  // present. On a side face, v runs downward from the top of the cube, so the rows above the tallest
  // blade must be entirely empty and the rows at the base entirely full, with a ramp between. A
  // uniformly sparse material would pass a simple "is it partly transparent" check while looking
  // nothing like grass, so the shape of the gradient is what is measured.
  const MaterialTemplate *tall = material_worlds_get(MATERIAL_TEMPLATE_GRASS_TALL);
  if (!tall)
  {
    check(false, "tall grass template exists");
  }
  else
  {
    const int top_opaque = count_opaque(tall->bake.texels[MATERIAL_FACE_TOP], total);
    printf("  (tall grass: top face %d%% opaque)\n", top_opaque * 100 / total);
    check(top_opaque > total / 10 && top_opaque < total / 2,
          "tall grass leaves most of its top face open, so ground shows between the tufts");

    int rows_opaque[MATERIAL_FACE_SIZE];
    for (int v = 0; v < MATERIAL_FACE_SIZE; v++)
    {
      int op = 0;
      for (int u = 0; u < MATERIAL_FACE_SIZE; u++)
        if ((tall->bake.texels[MATERIAL_FACE_LEFT][v * MATERIAL_FACE_SIZE + u] >> 24) != 0)
          op++;
      rows_opaque[v] = op;
    }

    check(rows_opaque[0] == 0 && rows_opaque[1] == 0 && rows_opaque[2] == 0,
          "above the tallest blade a side face is completely transparent");
    check(rows_opaque[MATERIAL_FACE_SIZE - 1] == MATERIAL_FACE_SIZE,
          "at the base of the tuft a side face is completely solid");

    // Monotone downward: no row may be less covered than the row above it. That is what makes the
    // boundary read as a grass line rather than as noise that happens to average out.
    bool monotone = true;
    for (int v = 1; v < MATERIAL_FACE_SIZE; v++)
      if (rows_opaque[v] < rows_opaque[v - 1])
        monotone = false;
    check(monotone, "side-face coverage only ever increases towards the base of the blades");
  }

  // The cube-filling materials should have no transparency anywhere: every ray hits at depth 0.
  const MaterialTemplateKind solid_through[] = {MATERIAL_TEMPLATE_SOIL, MATERIAL_TEMPLATE_GRANITE,
                                               MATERIAL_TEMPLATE_SANDSTONE,
                                               MATERIAL_TEMPLATE_WOOD_BARK};
  bool solid_all_opaque = true;
  for (size_t i = 0; i < sizeof(solid_through) / sizeof(solid_through[0]); i++)
  {
    const MaterialTemplate *t = material_worlds_get(solid_through[i]);
    if (!t) { solid_all_opaque = false; continue; }
    for (int f = 0; f < MATERIAL_FACE_COUNT; f++)
      if (count_opaque(t->bake.texels[f], total) != total) solid_all_opaque = false;
  }
  check(solid_all_opaque, "materials that fill the cube are opaque on every face");
}

// The three faces the isometric camera cannot see exist for the first-person camera, which stands
// inside the world. They have to be their own bakes: mirroring the opposite face would hang grass
// blades off the underside of an overhang, and a texel from the wrong side of the cube is a texel
// from the wrong depth into the material.
static void test_hidden_faces_are_their_own(void)
{
  printf("\nThe far three faces are baked, not borrowed\n"
         "------------------------------------------\n");

  const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
  const MaterialTemplate *grass = material_worlds_get(MATERIAL_TEMPLATE_GRASS);
  if (!grass)
  {
    check(false, "grass template exists");
    return;
  }

  // The top of grass is blades, the bottom is the soil they grow out of. If the bottom were the top
  // face reused, the two would be the same texels and the same colour balance.
  const bool differ = memcmp(grass->bake.texels[MATERIAL_FACE_TOP],
                             grass->bake.texels[MATERIAL_FACE_BOTTOM],
                             (size_t)total * sizeof(uint32_t)) != 0;
  check(differ, "grass bakes a different bottom face from its top");

  int top_green = 0, bottom_green = 0;
  for (int i = 0; i < total; i++)
  {
    if (is_greenish(grass->bake.texels[MATERIAL_FACE_TOP][i]))
      top_green++;
    if (is_greenish(grass->bake.texels[MATERIAL_FACE_BOTTOM][i]))
      bottom_green++;
  }
  printf("  (grass: %d green texels on top, %d underneath)\n", top_green, bottom_green);
  check(top_green > total / 4 && bottom_green == 0,
        "grass is green on top and bare soil underneath");

  // Every material must resolve all six faces, and the axis mapping must be a bijection: two axes
  // sharing a face would silently render one of them with the other's grain.
  bool mapping_ok = true;
  int seen[MATERIAL_FACE_COUNT] = {0};
  for (int axis = 0; axis < 3; axis++)
    for (int sign = 0; sign < 2; sign++)
    {
      const MaterialFace f = material_face_for_normal(axis, sign == 1);
      if (f < 0 || f >= MATERIAL_FACE_COUNT)
        mapping_ok = false;
      else
        seen[f]++;
    }
  for (int f = 0; f < MATERIAL_FACE_COUNT; f++)
    if (seen[f] != 1)
      mapping_ok = false;
  check(mapping_ok, "each of the six normals maps to its own face");
  check(material_face_for_normal(2, true) == MATERIAL_FACE_TOP &&
            material_face_for_normal(1, true) == MATERIAL_FACE_LEFT &&
            material_face_for_normal(0, true) == MATERIAL_FACE_RIGHT,
        "the three isometric faces keep their meaning: +Z top, +Y left, +X right");
}

// The CPU sampler is what the first-person renderer reads, and it has to agree with the texels the
// GPU atlas is built from or the two views would disagree about the same surface.
static void test_cpu_sampler(void)
{
  printf("\nCPU face sampling\n-----------------\n");

  const MaterialTemplate *stone = material_worlds_for_voxel(VOXEL_STONE);
  if (!stone)
  {
    check(false, "stone resolves to a template");
    return;
  }

  bool matches = true;
  for (int f = 0; f < MATERIAL_FACE_COUNT && matches; f++)
  {
    for (int v = 0; v < MATERIAL_FACE_SIZE && matches; v++)
      for (int u = 0; u < MATERIAL_FACE_SIZE; u++)
      {
        const uint32_t texel = stone->bake.texels[f][v * MATERIAL_FACE_SIZE + u];
        // Sample the centre of the texel, in the [0,1) the face spans.
        const float su = ((float)u + 0.5f) / (float)MATERIAL_FACE_SIZE;
        const float sv = ((float)v + 0.5f) / (float)MATERIAL_FACE_SIZE;
        uint8_t r = 0, g = 0, b = 0;
        const bool hit = material_worlds_sample(VOXEL_STONE, (MaterialFace)f, su, sv, &r, &g, &b);
        const bool opaque = (texel >> 24) != 0;
        if (hit != opaque ||
            (opaque && (r != ((texel >> 16) & 0xFF) || g != ((texel >> 8) & 0xFF) ||
                        b != (texel & 0xFF))))
        {
          matches = false;
          printf("  mismatch at face %s texel (%d,%d)\n", material_face_name((MaterialFace)f), u, v);
          break;
        }
      }
  }
  check(matches, "sampling every texel of every face returns the baked colour");

  // Whole units are one tile. This is what lets a greedy-merged quad spanning several voxels keep
  // per-voxel detail instead of stretching one tile over the whole run.
  uint8_t r0 = 0, g0 = 0, b0 = 0, r1 = 0, g1 = 0, b1 = 0;
  const bool a = material_worlds_sample(VOXEL_STONE, MATERIAL_FACE_TOP, 0.25f, 0.75f, &r0, &g0, &b0);
  const bool b_ = material_worlds_sample(VOXEL_STONE, MATERIAL_FACE_TOP, 7.25f, -3.25f, &r1, &g1, &b1);
  check(a && b_ && r0 == r1 && g0 == g1 && b0 == b1,
        "coordinates wrap, so one tile repeats per voxel and negative axes still land in it");

  uint8_t junk = 0;
  check(!material_worlds_sample(VOXEL_AIR, MATERIAL_FACE_TOP, 0.5f, 0.5f, &junk, &junk, &junk),
        "a material with no template reports no sample rather than a colour");
  check(!material_worlds_sample(VOXEL_STONE, (MaterialFace)MATERIAL_FACE_COUNT, 0.5f, 0.5f, &junk,
                                &junk, &junk),
        "an out-of-range face is refused rather than read out of bounds");
}

static void test_material_character(void)
{
  printf("\nEach material varies the way it should\n-------------------------------------\n");

  // Bark: ridges run along the trunk. On a side face v is the trunk axis, so colour should change
  // much more as u crosses the ridges than as v runs along them.
  const MaterialTemplate *bark = material_worlds_get(MATERIAL_TEMPLATE_WOOD_BARK);
  if (bark)
  {
    double u_var, v_var;
    directional_variation(bark->bake.texels[MATERIAL_FACE_RIGHT], &u_var, &v_var);
    printf("  (bark side face: across grain %.1f, along grain %.1f)\n", u_var, v_var);
    check(u_var > v_var, "bark varies more across its ridges than along the trunk");
  }
  else
  {
    check(false, "bark template available");
  }

  // Sandstone: bedding planes are horizontal, so on a side face the variation is the other way
  // round from bark — bands stack along v and run continuously across u.
  const MaterialTemplate *sandstone = material_worlds_get(MATERIAL_TEMPLATE_SANDSTONE);
  if (sandstone)
  {
    double u_var, v_var;
    directional_variation(sandstone->bake.texels[MATERIAL_FACE_RIGHT], &u_var, &v_var);
    printf("  (sandstone side face: along bedding %.1f, across bedding %.1f)\n", u_var, v_var);
    check(v_var > u_var, "sandstone varies more across its bedding planes than along them");
  }
  else
  {
    check(false, "sandstone template available");
  }

  // Granite is defined by grain rather than direction, so it should be speckled roughly equally
  // in both, and more finely than the banded materials.
  const MaterialTemplate *granite = material_worlds_get(MATERIAL_TEMPLATE_GRANITE);
  if (granite)
  {
    double u_var, v_var;
    directional_variation(granite->bake.texels[MATERIAL_FACE_TOP], &u_var, &v_var);
    const double ratio = (v_var > 0.0) ? u_var / v_var : 0.0;
    printf("  (granite top face: u %.1f, v %.1f, ratio %.2f)\n", u_var, v_var, ratio);
    check(ratio > 0.5 && ratio < 2.0, "granite speckle has no strong direction");
    check(u_var > 1.0, "granite speckle is actually visible");
  }
  else
  {
    check(false, "granite template available");
  }

  // Door: thin mid-plane panel — front/back faces show the boarded surface; side faces are mostly air.
  const MaterialTemplate *door = material_worlds_get(MATERIAL_TEMPLATE_DOOR);
  if (door)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int front_opaque = count_opaque(door->bake.texels[MATERIAL_FACE_FRONT], total);
    const int left_opaque = count_opaque(door->bake.texels[MATERIAL_FACE_LEFT], total);
    const int distinct =
        count_distinct_colours(door->bake.texels[MATERIAL_FACE_FRONT], total);
    printf("  (door: front %d%% opaque / %d colours, left %d%% opaque)\n",
           front_opaque * 100 / total, distinct, left_opaque * 100 / total);
    check(front_opaque > total / 2, "door front bake shows the panel");
    check(distinct >= 3, "door front bake has frame/panel/handle colour steps");
    check(left_opaque < total / 4, "door left bake is mostly open — a thin pane, not a block");
  }
  else
  {
    check(false, "door template available");
  }

  // Roof tile: angled shell — side face has an opaque diagonal band and transparent air above/below.
  // The top stays fully opaque (every column hits the shell) but depth-shades along the slope.
  const MaterialTemplate *roof = material_worlds_get(MATERIAL_TEMPLATE_ROOF_TILE);
  if (roof)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int left_opaque = count_opaque(roof->bake.texels[MATERIAL_FACE_LEFT], total);
    const int top_distinct =
        count_distinct_colours(roof->bake.texels[MATERIAL_FACE_TOP], total);
    printf("  (roof tile: top %d colours, left %d%% opaque)\n", top_distinct,
           left_opaque * 100 / total);
    check(top_distinct >= 3, "roof tile top shows slope/shingle colour variation");
    check(left_opaque > total / 10 && left_opaque < total / 2,
          "roof tile side shows an angled band with air above/below");
  }
  else
  {
    check(false, "roof tile template available");
  }

  // Thatch: same angled-shell contract.
  const MaterialTemplate *thatch = material_worlds_get(MATERIAL_TEMPLATE_THATCH);
  if (thatch)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int left_opaque = count_opaque(thatch->bake.texels[MATERIAL_FACE_LEFT], total);
    printf("  (thatch left face: %d%% opaque)\n", left_opaque * 100 / total);
    check(left_opaque > total / 10 && left_opaque < total / 2,
          "thatch side shows an angled band with air above/below");
  }
  else
  {
    check(false, "thatch template available");
  }

  // Glass: thin pane — front mostly opaque, left mostly empty.
  const MaterialTemplate *glass = material_worlds_get(MATERIAL_TEMPLATE_GLASS);
  if (glass)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int front_opaque = count_opaque(glass->bake.texels[MATERIAL_FACE_FRONT], total);
    const int left_opaque = count_opaque(glass->bake.texels[MATERIAL_FACE_LEFT], total);
    printf("  (glass: front %d%% opaque, left %d%% opaque)\n", front_opaque * 100 / total,
           left_opaque * 100 / total);
    check(front_opaque > total / 2, "glass front bake shows the pane");
    check(left_opaque < total / 4, "glass left bake is mostly open — a thin pane");
  }
  else
  {
    check(false, "glass template available");
  }

  // Crate: hollow box — walls show plank banding; nested world keeps an air cavity.
  const MaterialTemplate *crate = material_worlds_get(MATERIAL_TEMPLATE_CRATE);
  if (crate)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int front_opaque = count_opaque(crate->bake.texels[MATERIAL_FACE_FRONT], total);
    const int front_distinct =
        count_distinct_colours(crate->bake.texels[MATERIAL_FACE_FRONT], total);
    int cavity = 0;
    for (int z = 8; z < 24; z++)
      for (int y = 8; y < 24; y++)
        for (int x = 8; x < 24; x++)
        {
          const Voxel *sv = world_get_voxel(crate->world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
          if (!sv || sv->type == VOXEL_AIR)
            cavity++;
        }
    printf("  (crate: front %d%% opaque / %d colours, cavity air %d)\n",
           front_opaque * 100 / total, front_distinct, cavity);
    check(front_opaque > total / 4, "crate front shows plank walls");
    check(front_distinct >= 3, "crate front has plank/band colour steps");
    check(cavity > 1000, "crate nested world keeps a hollow interior");
  }
  else
  {
    check(false, "crate template available");
  }

  // Barrel: cylinder — top is a disc, not a full square.
  const MaterialTemplate *barrel = material_worlds_get(MATERIAL_TEMPLATE_BARREL);
  if (barrel)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int top_opaque = count_opaque(barrel->bake.texels[MATERIAL_FACE_TOP], total);
    printf("  (barrel top: %d%% opaque)\n", top_opaque * 100 / total);
    check(top_opaque > total / 5 && top_opaque < total * 3 / 4,
          "barrel top is a circular cap, not a sealed cube lid");
  }
  else
  {
    check(false, "barrel template available");
  }

  // Bed: mattress fills the footprint from above; sides show a low frame band with air above.
  const MaterialTemplate *bed = material_worlds_get(MATERIAL_TEMPLATE_BED);
  if (bed)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int top_distinct =
        count_distinct_colours(bed->bake.texels[MATERIAL_FACE_TOP], total);
    const int left_opaque = count_opaque(bed->bake.texels[MATERIAL_FACE_LEFT], total);
    printf("  (bed: top %d colours, left %d%% opaque)\n", top_distinct,
           left_opaque * 100 / total);
    check(top_distinct >= 2, "bed top shows mattress colour variation");
    check(left_opaque > total / 10 && left_opaque < total * 3 / 4,
          "bed side shows a low frame/mattress band with air above");
  }
  else
  {
    check(false, "bed template available");
  }

  // Stair: wedge — side face is a stepped band with air above the rise.
  const MaterialTemplate *stair = material_worlds_get(MATERIAL_TEMPLATE_STAIR);
  if (stair)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int left_opaque = count_opaque(stair->bake.texels[MATERIAL_FACE_LEFT], total);
    const int top_opaque = count_opaque(stair->bake.texels[MATERIAL_FACE_TOP], total);
    printf("  (stair: top %d%% opaque, left %d%% opaque)\n", top_opaque * 100 / total,
           left_opaque * 100 / total);
    check(left_opaque > total / 10 && left_opaque < total * 3 / 4,
          "stair side shows a stepped rise with air above");
    check(top_opaque > total / 8, "stair top shows tread surface");
  }
  else
  {
    check(false, "stair template available");
  }

  // Chair / table: mostly air on every face.
  const MaterialTemplate *chair = material_worlds_get(MATERIAL_TEMPLATE_CHAIR);
  if (chair)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int front_opaque = count_opaque(chair->bake.texels[MATERIAL_FACE_FRONT], total);
    printf("  (chair front: %d%% opaque)\n", front_opaque * 100 / total);
    check(front_opaque > total / 20 && front_opaque < total / 2,
          "chair front is a sparse furniture silhouette");
  }
  else
  {
    check(false, "chair template available");
  }

  const MaterialTemplate *table = material_worlds_get(MATERIAL_TEMPLATE_TABLE);
  if (table)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int top_opaque = count_opaque(table->bake.texels[MATERIAL_FACE_TOP], total);
    const int front_opaque = count_opaque(table->bake.texels[MATERIAL_FACE_FRONT], total);
    printf("  (table: top %d%% opaque, front %d%% opaque)\n", top_opaque * 100 / total,
           front_opaque * 100 / total);
    check(top_opaque > total / 4, "table top shows the slab");
    check(front_opaque < total / 2, "table front is mostly open under the top");
  }
  else
  {
    check(false, "table template available");
  }

  // Chest: inset solid box — top smaller than a full cube, sides partially open.
  const MaterialTemplate *chest = material_worlds_get(MATERIAL_TEMPLATE_CHEST);
  if (chest)
  {
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    const int top_opaque = count_opaque(chest->bake.texels[MATERIAL_FACE_TOP], total);
    const int front_opaque = count_opaque(chest->bake.texels[MATERIAL_FACE_FRONT], total);
    printf("  (chest: top %d%% opaque, front %d%% opaque)\n", top_opaque * 100 / total,
           front_opaque * 100 / total);
    check(top_opaque > total / 10 && top_opaque < total * 9 / 10,
          "chest top is an inset lid, not a sealed cube");
    check(front_opaque > total / 10 && front_opaque < total * 9 / 10,
          "chest front is an inset box silhouette");
  }
  else
  {
    check(false, "chest template available");
  }

  // Grass: soil host with a MATERIAL_GRASS_COAT_DEPTH turf coat. Top stays an opaque lawn; sides
  // carry a two-texel green rim. Checking the top is green-dominant and still varied.
  const MaterialTemplate *grass = material_worlds_get(MATERIAL_TEMPLATE_GRASS);
  if (grass)
  {
    int greenish = 0, earthy = 0;
    const uint32_t *texels = grass->bake.texels[MATERIAL_FACE_TOP];
    for (int i = 0; i < MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE; i++)
    {
      const uint8_t r = texel_r(texels[i]), g = texel_g(texels[i]), b = texel_b(texels[i]);
      if (g > r && g > b) greenish++;
      else if (r >= g) earthy++;
    }
    const int isolated = count_isolated_green(texels);
    const int total = MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE;
    printf("  (grass top face: %d green texels, %d earth texels, %d isolated blades)\n",
           greenish, earthy, isolated);
    check(greenish > total * 3 / 4, "grass top face is mostly green (opaque carpet, not soil holes)");
    check(earthy * 8 < greenish, "grass top face does not open onto bare earth between blades");
    check(isolated * 20 < greenish,
          "grass top face is patches of lawn, not a speckle of isolated blades");

    // Side rim: the top MATERIAL_GRASS_COAT_DEPTH rows of a side face are the coat (v=0 is +Z).
    const uint32_t *left = grass->bake.texels[MATERIAL_FACE_LEFT];
    int rim_green = 0, below_green = 0;
    for (int v = 0; v < MATERIAL_FACE_SIZE; v++)
    {
      for (int u = 0; u < MATERIAL_FACE_SIZE; u++)
      {
        const uint32_t t = left[v * MATERIAL_FACE_SIZE + u];
        if (!is_greenish(t))
          continue;
        if (v < MATERIAL_GRASS_COAT_DEPTH)
          rim_green++;
        else
          below_green++;
      }
    }
    printf("  (grass left face: %d green in coat rim, %d green below)\n", rim_green, below_green);
    check(rim_green > MATERIAL_FACE_SIZE * MATERIAL_GRASS_COAT_DEPTH / 2,
          "grass side face carries a green turf rim");
    check(below_green * 4 < rim_green,
          "grass side face below the coat rim is soil, not a cube of blades");

    // Nested world: coat occupies only the top MATERIAL_GRASS_COAT_DEPTH layers.
    int coat_cells = 0, deep_grass = 0;
    const int coat_z0 = MATERIAL_WORLD_SIZE - MATERIAL_GRASS_COAT_DEPTH;
    for (int z = 0; z < MATERIAL_WORLD_SIZE; z++)
      for (int y = 0; y < MATERIAL_WORLD_SIZE; y++)
        for (int x = 0; x < MATERIAL_WORLD_SIZE; x++)
        {
          const Voxel *sv = world_get_voxel(grass->world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
          if (!sv || !voxel_type_is_low_grass(sv->type))
            continue;
          if (z >= coat_z0)
            coat_cells++;
          else
            deep_grass++;
        }
    printf("  (grass world: %d coat cells, %d grass below coat)\n", coat_cells, deep_grass);
    check(coat_cells == MATERIAL_WORLD_SIZE * MATERIAL_WORLD_SIZE * MATERIAL_GRASS_COAT_DEPTH,
          "every column has a full MATERIAL_GRASS_COAT_DEPTH turf coat");
    check(deep_grass == 0, "no grass subvoxels below the coat — host is soil");
  }
  else
  {
    check(false, "grass template available");
  }

  const MaterialTemplate *tall_top = material_worlds_get(MATERIAL_TEMPLATE_GRASS_TALL);
  if (tall_top)
  {
    const uint32_t *texels = tall_top->bake.texels[MATERIAL_FACE_TOP];
    int greenish = 0;
    for (int i = 0; i < MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE; i++)
      if (texel_a(texels[i]) && is_greenish(texels[i]))
        greenish++;
    const int isolated = count_isolated_green(texels);
    printf("  (tall grass top face: %d green texels, %d isolated blades)\n", greenish, isolated);
    check(greenish > 0, "tall grass top face has blade tips");
    check(isolated * 10 < greenish,
          "tall grass top face is tuft patches, not a speckle of isolated blade tips");
  }
  else
  {
    check(false, "tall grass template available");
  }
}

static void test_bake_rejects_wrong_size(void)
{
  printf("\nBake input validation\n---------------------\n");

  MaterialFaceBake bake;
  check(!material_world_bake_faces(NULL, &bake), "baking NULL world fails");

  const MaterialTemplate *t = material_worlds_get(MATERIAL_TEMPLATE_STONE);
  check(t && !material_world_bake_faces(t->world, NULL), "baking to NULL output fails");

  World *wrong = world_create(16, 16, 16);
  check(wrong && !material_world_bake_faces(wrong, &bake),
        "baking a world that is not 32x32x32 fails rather than reading out of bounds");
  world_destroy(wrong);
}

static void test_low_grass_coat_break(void)
{
  printf("\nLow-grass coat break semantics\n------------------------------\n");
  check(voxel_type_is_low_grass(VOXEL_GRASS), "VOXEL_GRASS is a low-grass coat");
  check(voxel_type_is_low_grass(VOXEL_GRASS_MOSS), "moss is a low-grass coat");
  check(!voxel_type_is_low_grass(VOXEL_GRASS_TALL), "tall grass is a prop, not a coat");
  check(voxel_type_after_break(VOXEL_GRASS) == VOXEL_SOIL,
        "breaking low grass scrapes to soil");
  check(voxel_type_after_break(VOXEL_GRASS_WIDE) == VOXEL_SOIL,
        "grass variants scrape to soil");
  check(voxel_type_after_break(VOXEL_STONE) == VOXEL_AIR,
        "breaking stone still excavates to air");
  check(MATERIAL_GRASS_COAT_DEPTH == 2, "coat depth is two subvoxels");
}

static bool tmpl_cell_empty(const MaterialTemplate *t, int x, int y, int z)
{
  if (!t || !t->world)
    return true;
  const Voxel *v = world_voxel_cptr_fast(t->world, x, y, z);
  return !v || v->type == VOXEL_AIR;
}

static bool tmpl_cell_solid(const MaterialTemplate *t, int x, int y, int z)
{
  return !tmpl_cell_empty(t, x, y, z);
}

static int tmpl_solid_count(const MaterialTemplate *t)
{
  if (!t || !t->world)
    return 0;
  const int S = MATERIAL_WORLD_SIZE;
  int n = 0;
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
        if (tmpl_cell_solid(t, x, y, z))
          n++;
  return n;
}

// Nested templates must be true 3D silhouettes — not a sealed cube that only looks round because
// of circular face textures. Parent FP meshing then instances these meshes at 1/32 scale.
static void test_sparse_silhouettes(void)
{
  printf("\nSparse nested silhouettes\n-------------------------\n");
  const int S = MATERIAL_WORLD_SIZE;
  const int vol = S * S * S;

  check(material_template_kind_is_sparse(MATERIAL_TEMPLATE_BUSH), "bush kind is sparse");
  check(material_template_kind_is_sparse(MATERIAL_TEMPLATE_ROOF_TILE), "roof kind is sparse");
  check(material_template_kind_is_sparse(MATERIAL_TEMPLATE_STAIR), "stair kind is sparse");
  check(!material_template_kind_is_sparse(MATERIAL_TEMPLATE_STONE), "stone is not sparse");

  const MaterialTemplate *bush = material_worlds_get(MATERIAL_TEMPLATE_BUSH);
  check(bush && bush->world, "bush template world exists");
  if (bush && bush->world)
  {
    const int solids = tmpl_solid_count(bush);
    printf("  (bush fill %d / %d = %d%%)\n", solids, vol, solids * 100 / vol);
    check(solids > vol / 20 && solids < vol / 2,
          "bush fills a round clump, not the whole parent cube");
    // Outer AABB corners stay empty — a face-circle-on-cube bake would still fill those cells.
    check(tmpl_cell_empty(bush, 0, 0, S / 2) && tmpl_cell_empty(bush, S - 1, 0, S / 2) &&
              tmpl_cell_empty(bush, 0, S - 1, S / 2) && tmpl_cell_empty(bush, S - 1, S - 1, S / 2),
          "bush AABB side-corners are empty (sphere, not cube)");
    check(tmpl_cell_solid(bush, S / 2, S / 2, S / 2) || tmpl_cell_solid(bush, S / 2, S / 2, S / 2 - 2),
          "bush has a solid core near mid-height");
  }

  const MaterialTemplate *stair = material_worlds_get(MATERIAL_TEMPLATE_STAIR);
  check(stair && stair->world, "stair template world exists");
  if (stair && stair->world)
  {
    const int solids = tmpl_solid_count(stair);
    printf("  (stair fill %d / %d = %d%%)\n", solids, vol, solids * 100 / vol);
    check(solids > vol / 20 && solids < (vol * 3) / 4, "stair is a wedge, not a sealed cube");
    // Rising along +x: low-x top cells empty, high-x bottom cells solid.
    check(tmpl_cell_empty(stair, 2, S / 2, S - 2), "stair leaves space above the first tread");
    check(tmpl_cell_solid(stair, S - 3, S / 2, 1) || tmpl_cell_solid(stair, S - 4, S / 2, 2),
          "stair has solid steps near the high end");
  }

  const MaterialTemplate *roof = material_worlds_get(MATERIAL_TEMPLATE_ROOF_TILE);
  check(roof && roof->world, "roof template world exists");
  if (roof && roof->world)
  {
    const int solids = tmpl_solid_count(roof);
    printf("  (roof fill %d / %d = %d%%)\n", solids, vol, solids * 100 / vol);
    check(solids > vol / 40 && solids < vol / 2, "roof is a shell, not a filled cube");
    check(tmpl_cell_empty(roof, S / 2, S / 2, 2), "roof interior under the pitch is empty");
  }

  // Parent meshing omits fitting AABBs (doors/roofs/…); vegetation keeps its cube. Template
  // meshing must keep nested sparse cells.
  World *parent = world_create(8, 8, 8);
  check(parent != NULL, "parent fixture world created");
  if (parent)
  {
    world_set_voxel(parent, 3, 3, 3, VOXEL_DOOR);
    world_set_voxel(parent, 5, 3, 3, VOXEL_BUSH);
    world_set_voxel(parent, 4, 3, 3, VOXEL_STONE);
    VoxelMesh mesh_skip = {0};
    VoxelMesh mesh_keep = {0};
    voxel_mesh_init(&mesh_skip);
    voxel_mesh_init(&mesh_keep);
    voxel_mesh_build_all_faces_greedy_ex(parent, &mesh_skip, true);
    voxel_mesh_build_all_faces_greedy_ex(parent, &mesh_keep, false);

    int door_quads_skip = 0, door_quads_keep = 0, bush_quads_skip = 0, stone_quads_skip = 0;
    for (int i = 0; i < mesh_skip.count; i++)
    {
      if (mesh_skip.quads[i].type == VOXEL_DOOR)
        door_quads_skip++;
      if (mesh_skip.quads[i].type == VOXEL_BUSH)
        bush_quads_skip++;
      if (mesh_skip.quads[i].type == VOXEL_STONE)
        stone_quads_skip++;
    }
    for (int i = 0; i < mesh_keep.count; i++)
      if (mesh_keep.quads[i].type == VOXEL_DOOR)
        door_quads_keep++;

    check(door_quads_skip == 0, "skip_sparse omits door AABB quads from parent mesh");
    check(bush_quads_skip > 0, "skip_sparse still emits bush AABB quads (vegetation fallback)");
    check(stone_quads_skip > 0, "skip_sparse still emits solid stone quads");
    check(door_quads_keep > 0, "without skip_sparse, door still emits parent AABB quads");
    check(world_voxel_omits_parent_aabb(world_voxel_cptr_fast(parent, 3, 3, 3)),
          "door omits parent AABB");
    check(!world_voxel_omits_parent_aabb(world_voxel_cptr_fast(parent, 5, 3, 3)),
          "bush keeps parent AABB");
    check(world_voxel_needs_nested_silhouette(world_voxel_cptr_fast(parent, 5, 3, 3)),
          "bush still counts as nested-silhouette material (gaps in bake)");

    voxel_mesh_free(&mesh_skip);
    voxel_mesh_free(&mesh_keep);
    world_destroy(parent);
  }

  if (bush && bush->world)
  {
    VoxelMesh nested = {0};
    voxel_mesh_init(&nested);
    voxel_mesh_build_all_faces_greedy_ex(bush->world, &nested, false);
    check(nested.count > 20, "bush nested mesh has a real silhouette (many quads)");
    // A sealed cube of 32³ has six faces; a sphere needs many more small faces.
    check(nested.count > 6, "bush nested mesh is not a six-face cube");
    voxel_mesh_free(&nested);
  }
}

int main(void)
{
  printf("=== Material Template World Tests ===\n");

  test_registry();
  test_voxel_mapping();
  test_determinism();
  test_faces_carry_detail();
  test_hidden_faces_are_their_own();
  test_cpu_sampler();
  test_material_character();
  test_bake_rejects_wrong_size();
  test_low_grass_coat_break();
  test_sparse_silhouettes();

  material_worlds_shutdown();
  check(material_worlds_get(MATERIAL_TEMPLATE_GRASS) == NULL,
        "shutdown clears the registry");

  printf("\n");
  if (g_failures == 0)
    printf("=== ALL PASSED (%d checks) ===\n", g_checks);
  else
    printf("=== %d of %d checks FAILED ===\n", g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
