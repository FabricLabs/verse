#include "material_worlds.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------
// Deterministic noise
//
// The same FNV-1a mixing the terrain generator uses, so a template world is reproducible from its
// kind alone: no seed is threaded through, because every grass voxel in the universe shares one
// template and there is nothing to vary per instance.
// ---------------------------------------------------------------------------------------------

static uint32_t mat_hash(uint32_t a, uint32_t b, uint32_t c, uint32_t salt)
{
  uint32_t h = 2166136261u ^ salt;
  h = (h ^ a) * 16777619u;
  h = (h ^ b) * 16777619u;
  h = (h ^ c) * 16777619u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return h;
}

// Uniform in [0,1).
static float mat_rand01(int x, int y, int z, uint32_t salt)
{
  return (float)(mat_hash((uint32_t)x, (uint32_t)y, (uint32_t)z, salt) >> 8) / 16777216.0f;
}

// Value noise on a lattice of `period` sub-voxels, smoothed. Wraps on the template's size so a
// face tiles seamlessly against the neighbouring voxel of the same material.
static float mat_noise_2d(int x, int y, int period, uint32_t salt)
{
  const int wrap = MATERIAL_WORLD_SIZE;
  const float fx = (float)x / (float)period;
  const float fy = (float)y / (float)period;
  const int x0 = (int)fx, y0 = (int)fy;
  const float tx = fx - (float)x0, ty = fy - (float)y0;

  // Smoothstep so the lattice does not show as straight seams.
  const float sx = tx * tx * (3.0f - 2.0f * tx);
  const float sy = ty * ty * (3.0f - 2.0f * ty);

  const int lattice = wrap / period > 0 ? wrap / period : 1;
  const int xa = x0 % lattice, xb = (x0 + 1) % lattice;
  const int ya = y0 % lattice, yb = (y0 + 1) % lattice;

  const float v00 = mat_rand01(xa, ya, 0, salt);
  const float v10 = mat_rand01(xb, ya, 0, salt);
  const float v01 = mat_rand01(xa, yb, 0, salt);
  const float v11 = mat_rand01(xb, yb, 0, salt);

  const float a = v00 + (v10 - v00) * sx;
  const float b = v01 + (v11 - v01) * sx;
  return a + (b - a) * sy;
}

// ---------------------------------------------------------------------------------------------
// Template generation
//
// Each of these fills a 32^3 world so that its *surfaces* read as the material. What matters is
// the top few sub-voxel layers on each face, because that is what the bake sees; the interior is
// filled so that a future path which marches into a parent voxel finds sensible material there.
// ---------------------------------------------------------------------------------------------

#define S MATERIAL_WORLD_SIZE

// The soil at one sub-voxel: clumped pockets of clay and silt with the odd pebble, so soil is not
// one flat brown. Shared with the grass template, whose base is the same material — without it, the
// underside of a grass voxel baked to a single colour, and a cliff overhang came out a flat slab.
static VoxelType soil_type_at(int x, int y, int z)
{
  const float clump = mat_noise_2d(x, y, 8, 0x1122u + (uint32_t)(z / 6));
  if (clump > 0.72f)
    return VOXEL_SOIL_CLAY;
  if (clump < 0.30f)
    return VOXEL_SOIL_SILT;
  if (mat_rand01(x, y, z, 0x9F3Bu) < 0.04f)
    return VOXEL_GRAVEL_GRANITE; // small stones through the soil
  return VOXEL_SOIL;
}

// Low grass: a soil host with a MATERIAL_GRASS_COAT_DEPTH turf coat on top.
//
// The parent voxel is ground, not a prop — so the nested world is mostly dirt, and only the top
// two sub-voxels are green. That is sod depth on the 32³ lattice: the top bake stays an opaque
// lawn (carpet every column), side faces show a two-texel green rim over soil, and breaking the
// parent can scrape the coat without inventing a second embedded soil band that fought the real
// soil voxels under the world cap.
//
// Cover and colour come from wrapping noise so neighbouring columns agree. Independent
// per-column hashes made the top face a speckle of one-voxel blades.
static void gen_grass(World *w)
{
  const int coat_z0 = S - MATERIAL_GRASS_COAT_DEPTH;
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++)
    {
      // Host fill: clumped soil, with a thin loam A-horizon under the coat.
      for (int z = 0; z < coat_z0; z++)
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z,
                        (z < coat_z0 - 3) ? soil_type_at(x, y, z) : VOXEL_SOIL_LOAM);

      const float cover = mat_noise_2d(x, y, 8, 0x6B1Au);
      const VoxelType carpet =
          (cover < 0.22f) ? VOXEL_GRASS_MOSS
                          : ((mat_noise_2d(x, y, 4, 0x77E1u) < 0.5f) ? VOXEL_GRASS
                                                                     : VOXEL_GRASS_WIDE);
      // Both coat layers share the column's species so the side rim stays one green band. The tip
      // layer alone picks a related blade so the top bake keeps lawn continuity without reading as
      // a single flat green (the FP audit's textured-floor check).
      for (int z = coat_z0; z < S; z++)
      {
        VoxelType t = carpet;
        if (z == S - 1)
        {
          const float tip = mat_noise_2d(x, y, 3, 0xA11Eu);
          if (tip < 0.18f)
            t = VOXEL_GRASS_MOSS;
          else if (tip < 0.36f)
            t = VOXEL_GRASS_CLOVER;
          else if (tip > 0.82f)
            t = VOXEL_GRASS_SHARP;
          else if (tip > 0.64f)
            t = VOXEL_GRASS_WIDE;
        }
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
    }
}

// Soil: clumped, with pebbles and pockets of clay and silt so it is not one flat brown.
static void gen_soil(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, soil_type_at(x, y, z));
}

// Stone: mostly uniform with darker inclusions and a slightly pitted surface, so a face has some
// relief rather than reading as a solid grey tile.
static void gen_stone(World *w)
{
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++)
    {
      // Pit the top surface by a sub-voxel or two.
      const int top = S - 1 - (int)(mat_noise_2d(x, y, 6, 0x3311u) * 2.5f);
      for (int z = 0; z <= top; z++)
      {
        VoxelType t = VOXEL_STONE;
        const float v = mat_noise_2d(x + z * 3, y - z * 2, 8, 0x4455u);
        if (v > 0.78f)
          t = VOXEL_STONE_BASALT;
        else if (v < 0.22f)
          t = VOXEL_STONE_LIMESTONE;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
    }
}

// Granite: coarse mineral grain. Speckle at a small lattice, which is what distinguishes granite
// from generic stone at a glance — light feldspar, dark biotite, mid quartz.
static void gen_granite(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        // Grains are 2-3 sub-voxels across: quantise the coordinate so neighbours share a grain
        // instead of every sub-voxel being independently coloured, which would read as noise.
        const int gx = x / 2, gy = y / 2, gz = z / 2;
        const float g = mat_rand01(gx, gy, gz, 0x6E11u);
        VoxelType t;
        if (g < 0.34f)
          t = VOXEL_STONE_GRANITE;
        else if (g < 0.62f)
          t = VOXEL_STONE_LIMESTONE; // pale grain
        else if (g < 0.86f)
          t = VOXEL_STONE;
        else
          t = VOXEL_STONE_BASALT; // dark grain
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Sandstone: bedding planes. Horizontal bands of slightly different grain, with the band
// boundaries wavering so they do not look like drawn lines.
static void gen_sandstone(World *w)
{
  for (int z = 0; z < S; z++)
  {
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        // Waver the band boundary by a sub-voxel or two across the face.
        const float waver = mat_noise_2d(x, y, 16, 0x2B7Cu) * 2.0f;
        const int band = (int)(((float)z + waver) / 4.0f);
        VoxelType t = (band % 2 == 0) ? VOXEL_STONE_SANDSTONE : VOXEL_SAND;
        // Grain speckle within a band.
        if (mat_rand01(x, y, z, 0x88C1u) < 0.10f)
          t = (t == VOXEL_STONE_SANDSTONE) ? VOXEL_GRAVEL_SANDSTONE : VOXEL_STONE_SANDSTONE;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
  }
}

// Bark: fibres running along the trunk. The trunk axis is z, so the ridges are vertical on the
// side faces — which is the whole point, and why bark cannot be a single tiled texture shared
// between the top and side faces. The top face is the cut end, so it gets rings instead.
//
// The species types are picked for contrast, not botany: VOXEL_WOOD and VOXEL_WOOD_OAK resolve to
// the same colour, so building the rings out of that pair produced a template whose structure was
// real in the voxels and invisible once baked.
static void gen_wood_bark(World *w)
{
  const float centre = (float)S * 0.5f - 0.5f;

  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float dx = (float)x - centre;
        const float dy = (float)y - centre;
        const float radius = __builtin_sqrtf(dx * dx + dy * dy);

        // Growth rings on the cut end: concentric, spacing wavering so they are not perfect
        // circles. VOXEL_WOOD is mid brown, VOXEL_WOOD_PINE is darker.
        const float ring_waver = mat_noise_2d(x, y, 16, 0x30F1u) * 1.5f;
        const int ring = (int)((radius + ring_waver) / 2.5f);
        VoxelType t = (ring % 2 == 0) ? VOXEL_WOOD : VOXEL_WOOD_PINE;

        // Bark ridges on the outside: rougher material at the rim, grooved along the trunk.
        if (radius > (float)S * 0.40f)
        {
          // Two terms, and the balance between them is the whole character of bark. `around` varies
          // as you go round the trunk and is what makes the grain read as vertical. `along` drifts
          // slowly up the trunk so a ridge wanders and breaks rather than running as a perfectly
          // straight stripe — without it, a side face is one row of colours repeated 32 times,
          // since the face pins one axis to the rim and leaves the pattern one-dimensional.
          // `around` is weighted the heavier of the two so the grain still runs the right way.
          const float around = (mat_noise_2d(x, y, 4, 0x5D2Fu) +
                                mat_noise_2d(x, y, 8, 0x1E77u) * 0.5f) / 1.5f;
          const float along = mat_noise_2d(x + z / 4, y + z / 6, 8, 0x44A9u);
          const float g = around * 0.72f + along * 0.28f;

          // Three steps rather than two, so a ridge has a lit crest, a flank and a dark groove.
          if (g < 0.38f)
            t = VOXEL_WOOD_PINE; // groove, darkest
          else if (g < 0.66f)
            t = VOXEL_WOOD; // flank
          else
            t = VOXEL_WOOD_WALNUT; // crest, lightest
        }
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Leaves: mostly gaps. A canopy voxel should read as foliage with sky through it, so this is
// deliberately sparse and the bake leaves the empty texels transparent.
static void gen_leaves(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float clump = mat_noise_2d(x + z, y - z, 6, 0x7A31u);
        if (clump < 0.42f)
          continue; // gap
        const VoxelType t = (mat_rand01(x, y, z, 0x3C09u) < 0.30f) ? VOXEL_LEAVES_OAK : VOXEL_LEAVES;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Bushes: denser than canopy leaves, still with gaps. A shrub should read as a leafy clump with
// ground showing between stems, not a solid green cube. Fill is a roundish volume (fatter than
// leaves' scatter) so the top bake stays a patchy canopy rather than an empty lid.
static void gen_bush(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float nx = ((float)x + 0.5f) / (float)S - 0.5f;
        const float ny = ((float)y + 0.5f) / (float)S - 0.5f;
        const float nz = ((float)z + 0.5f) / (float)S; // 0 at base, 1 at top
        const float radial = nx * nx + ny * ny;
        // Round shrub: widest near mid-height, taper at the crown, thin stem near the ground.
        float shape = radial * 2.6f + (nz - 0.48f) * (nz - 0.48f) * 1.6f;
        if (nz < 0.10f)
          shape += 0.35f;
        if (shape > 0.50f)
          continue;
        const float clump = mat_noise_2d(x + z * 2, y - z, 5, 0x5B2Eu);
        // Denser than gen_leaves (which skips below 0.42).
        if (clump < 0.16f)
          continue;
        VoxelType t = VOXEL_BUSH;
        const float tip = mat_rand01(x, y, z, 0x91A7u);
        if (tip < 0.18f)
          t = VOXEL_BUSH_FERN;
        else if (tip < 0.30f)
          t = VOXEL_BUSH_VINES;
        else if (tip > 0.88f)
          t = VOXEL_BUSH_THORNS;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Tall grass: blades and nothing else.
//
// The difference from gen_grass is what is *not* here — there is no soil. A tuft stands in open air
// on top of the ground, so every sub-voxel that is not a blade has to stay empty and bake
// transparent, which is what lets the terrain behind and below a tuft show through it. That also
// makes the height of each blade visible as the height at which the voxel stops being opaque:
// above the tallest blade in a column there is simply nothing to find.
//
// Blades rise from the bottom of the cube because the tuft sits on the surface below it.
//
// Placement is two wrapping noises rather than an independent coin-flip per column: a slow clump
// decides where tufts sit, a tighter one fattens their interiors. From above a tuft is then a
// connected patch, not a scatter of single-pixel blade tips. Colour is per-column too, so a tip
// is one green rather than a vertical stack of coin-flips.
static void gen_grass_tall(World *w)
{
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++)
    {
      const float clump = mat_noise_2d(x, y, 6, 0x3E7Du);
      const float core = mat_noise_2d(x, y, 3, 0x51C7u);
      if (clump < 0.52f)
        continue;
      // Thin only the fringe so a tuft's interior stays solid from above. Applying the
      // tighter noise everywhere punched holes in the middle of a clump, which read as
      // artifacts on the top face.
      if (clump < 0.64f && core < 0.40f)
        continue;

      // Height is high throughout a tuft so the top face sees a patch at a similar depth, not a
      // mix of short dark blades and tall bright ones. Residual variation follows the clump so
      // the side silhouette is still uneven rather than a flat-topped block.
      // z must stay below 29: the top three side-face rows have to remain empty.
      const int height = 20 + (int)((clump - 0.52f) / 0.48f * 9.0f); // 20-29
      const VoxelType blade =
          (mat_noise_2d(x, y, 4, 0x2B84u) < 0.45f) ? VOXEL_GRASS_TALL : VOXEL_GRASS_WIDE;
      for (int z = 0; z < height && z < 29; z++)
      {
        // Darker towards the base, where a blade is in the shade of its neighbours.
        const VoxelType t = (z < height / 4) ? VOXEL_GRASS_MOSS : blade;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
    }
}

// Sand: fine grain with ripples across the top face.
static void gen_sand(World *w)
{
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++)
    {
      // Ripples: a low sinusoid-ish drift built from the noise lattice.
      const int top = S - 1 - (int)(mat_noise_2d(x, y, 8, 0x2277u) * 2.0f +
                                    mat_noise_2d(x * 2, y, 16, 0x91A4u) * 1.5f);
      for (int z = 0; z <= top; z++)
      {
        const VoxelType t = (mat_rand01(x, y, z, 0x4E62u) < 0.08f) ? VOXEL_GRAVEL_SANDSTONE : VOXEL_SAND;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
    }
}

// Thatch / clay roof: a thin shell along a diagonal plane so each stepped roof voxel reads as an
// angled surface rather than a full cube. `mirror` flips the slope for the far side of a gable.
// Solid only near the plane; air above and below survives the bake (seal_empty = false).
static void gen_roof_shell(World *w, bool thatch, bool mirror)
{
  const int thickness = thatch ? 5 : 4;
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++)
    {
      const int along = mirror ? (S - 1 - x) : x;
      // Slope rises along +along: low at the eaves edge, high toward the ridge.
      const int surface_z = 2 + (along * (S - 5)) / (S > 1 ? (S - 1) : 1);
      for (int z = 0; z < S; z++)
      {
        if (z < surface_z - thickness || z > surface_z)
          continue;

        VoxelType t;
        if (thatch)
        {
          const float waver = mat_noise_2d(x, y, 8, 0x7A7Cu) * 1.5f;
          const int course = (int)(((float)y + waver) / 4.0f);
          t = (course % 2 == 0) ? VOXEL_THATCH : VOXEL_STRAW;
          const float strand = mat_noise_2d(along, y / 2, 3, 0x57A1u);
          if (strand < 0.18f)
            t = VOXEL_WOOD_PINE;
          else if (strand > 0.82f)
            t = VOXEL_STRAW;
          // Darker underside of the thatch mat.
          if (z < surface_z - thickness + 2)
            t = VOXEL_WOOD_PINE;
        }
        else
        {
          const int row = y / 4;
          const int stagger = (row % 2) * 2;
          const int col = (along + stagger) / 5;
          const int lx = (along + stagger) % 5;
          const int ly = y % 4;
          t = VOXEL_ROOF_TILE;
          if (lx == 0 || lx == 4)
            t = VOXEL_BRICK;
          else if (ly == 0 || ly == 3 || z == surface_z)
            t = VOXEL_TERRACOTTA; // exposed lip
          else if ((lx + ly + col) % 5 == 2)
            t = VOXEL_ADOBE;
          if (z < surface_z - thickness + 2)
            t = VOXEL_TERRACOTTA; // underside
        }
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
    }
}

static void gen_thatch(World *w) { gen_roof_shell(w, true, false); }
static void gen_thatch_mirror(World *w) { gen_roof_shell(w, true, true); }
static void gen_roof_tile(World *w) { gen_roof_shell(w, false, false); }
static void gen_roof_tile_mirror(World *w) { gen_roof_shell(w, false, true); }

// Door: a thin boarded panel in the mid-plane of the cube (air on both sides) so the bake
// silhouette is a hung door rather than a solid block. `ns` puts the pane in the Y mid-plane for
// openings in north/south walls; otherwise the pane sits in the X mid-plane for east/west walls.
static void gen_door_oriented(World *w, bool ns)
{
  const int pane0 = S / 2 - 1;
  const int pane1 = S / 2 + 1;
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int along = ns ? y : x;
        const int across = ns ? x : y;
        if (along < pane0 || along > pane1)
          continue;
        const int u = across;
        const int v = z;
        const bool frame = (u < 3 || u >= S - 3 || v < 3 || v >= S - 3);
        VoxelType t;
        if (frame)
        {
          t = VOXEL_WOOD_OAK;
        }
        else
        {
          const bool upper = (v >= S / 2);
          const int pv0 = upper ? (S / 2 + 2) : 5;
          const int pv1 = upper ? (S - 5) : (S / 2 - 2);
          const bool in_panel = (u >= 6 && u < S - 6 && v >= pv0 && v < pv1);
          if (in_panel)
            t = VOXEL_WOOD_PINE;
          else
          {
            const int board = u / 4;
            t = (board % 2 == 0) ? VOXEL_PLANK : VOXEL_WOOD;
          }
        }
        if (u >= S - 6 && u < S - 4 && v >= S / 2 - 2 && v <= S / 2 + 2)
          t = VOXEL_STONE_BASALT; // handle
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_door(World *w) { gen_door_oriented(w, false); }
static void gen_door_ns(World *w) { gen_door_oriented(w, true); }

// Glass: a thin pane through the cube mid-plane. Air on both sides bakes transparent so a window
// reads as a sheet rather than a solid glass block.
static void gen_glass_oriented(World *w, bool ns)
{
  const int pane0 = S / 2 - 1;
  const int pane1 = S / 2 + 1;
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int along = ns ? y : x;
        const int across = ns ? x : y;
        if (along < pane0 || along > pane1)
          continue;
        const bool muntin =
            (across % 8 == 0) || (z % 8 == 0) || (across < 2 || across >= S - 2 || z < 2 || z >= S - 2);
        VoxelType t;
        if (muntin)
          t = VOXEL_WOOD_OAK;
        else if (mat_rand01(x, y, z, 0x61A5u) < 0.15f)
          t = VOXEL_GLASS_WHITE; // slight frost variation
        else
          t = VOXEL_GLASS;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_glass(World *w) { gen_glass_oriented(w, false); }
static void gen_glass_ns(World *w) { gen_glass_oriented(w, true); }

// Crate: hollow wooden box with plank walls and an open-ish top rim.
static void gen_crate(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const bool wall = (x < 3 || x >= S - 3 || y < 3 || y >= S - 3);
        const bool floor = (z < 3);
        const bool rim = (z >= S - 4 && z < S - 1);
        if (!wall && !floor && !rim)
          continue;
        if (!wall && !floor && rim && (x > 5 && x < S - 6 && y > 5 && y < S - 6))
          continue; // open top
        // Board mix so every face (including the underside) has colour steps.
        const int grain = (x * 3 + y * 5 + z * 7) % 5;
        VoxelType t = VOXEL_WOOD;
        if (grain == 0 || grain == 1)
          t = VOXEL_PLANK;
        else if (grain == 2)
          t = VOXEL_WOOD_PINE;
        else if (grain == 3)
          t = VOXEL_WOOD_OAK;
        if (floor && (x + y) % 3 == 0)
          t = VOXEL_WOOD_PINE;
        if (wall && z > 0 && z % 8 == 0)
          t = VOXEL_IRON; // banding
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Barrel: vertical cylinder shell with iron hoops.
static void gen_barrel(World *w)
{
  const float centre = (float)S * 0.5f - 0.5f;
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float dx = (float)x - centre;
        const float dy = (float)y - centre;
        const float r = __builtin_sqrtf(dx * dx + dy * dy);
        const float outer = (float)S * 0.42f;
        const float inner = outer - 3.0f;
        const bool shell = (r <= outer && r >= inner);
        const bool cap = (r <= outer && (z < 3 || z >= S - 3));
        if (!shell && !cap)
          continue;
        VoxelType t = VOXEL_BARREL;
        if (z == 8 || z == 16 || z == 24)
          t = VOXEL_IRON; // hoop
        else if ((int)(r * 3.0f) % 2 == 0)
          t = VOXEL_WOOD_OAK;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Bed: plank frame with a soft wool/straw mattress mound on top.
static void gen_bed(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const bool frame = (z < 6) && (x < 2 || x >= S - 2 || y < 2 || y >= S - 2 || z < 2);
        const bool mattress = (z >= 6 && z < 14);
        if (!frame && !mattress)
          continue;
        if (mattress)
        {
          // Soft dome — thinner toward the edges.
          const float cx = (float)S * 0.5f - 0.5f;
          const float cy = (float)S * 0.5f - 0.5f;
          const float dx = ((float)x - cx) / (float)(S / 2);
          const float dy = ((float)y - cy) / (float)(S / 2);
          const float h = 14.0f - (dx * dx + dy * dy) * 4.0f;
          if ((float)z > h)
            continue;
          VoxelType t = ((x + y + z) % 3 == 0) ? VOXEL_WOOL_WHITE : VOXEL_STRAW;
          if (z >= 12)
            t = VOXEL_WOOL_BROWN; // blanket fold
          world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
        }
        else
        {
          // Mixed timber so the underside isn't a single flat colour.
          const int grain = (x * 2 + y * 3 + z) % 3;
          VoxelType ft = VOXEL_PLANK;
          if (grain == 1)
            ft = VOXEL_WOOD;
          else if (grain == 2)
            ft = VOXEL_WOOD_OAK;
          world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, ft);
        }
      }
}

// Stair: solid stepped wedge rising along +along (X or Y). Air above the tread survives the bake
// so the silhouette is a half-cell rise rather than a full cube.
static void gen_stair_oriented(World *w, bool ns)
{
  const int steps = 4;
  const int step_run = S / steps;
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int along = ns ? y : x;
        const int across = ns ? x : y;
        const int step = along / (step_run > 0 ? step_run : 1);
        const int rise = (step + 1) * (S / steps);
        if (z >= rise)
          continue;
        // Leave a small air band on the sides so the tread reads inset.
        if (across < 1 || across >= S - 1)
          continue;
        VoxelType t = VOXEL_PLANK;
        const int grain = (along + across + z) % 4;
        if (grain == 0)
          t = VOXEL_WOOD;
        else if (grain == 1)
          t = VOXEL_WOOD_OAK;
        else if (z == rise - 1)
          t = VOXEL_WOOD_PINE; // tread lip
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_stair(World *w) { gen_stair_oriented(w, false); }
static void gen_stair_ns(World *w) { gen_stair_oriented(w, true); }

// Chair: four legs, a seat slab, and a backrest — mostly air so the bake silhouette is furniture.
static void gen_chair(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const bool leg = (z < 14) &&
                         ((x >= 4 && x < 7 && y >= 4 && y < 7) ||
                          (x >= S - 7 && x < S - 4 && y >= 4 && y < 7) ||
                          (x >= 4 && x < 7 && y >= S - 7 && y < S - 4) ||
                          (x >= S - 7 && x < S - 4 && y >= S - 7 && y < S - 4));
        const bool seat = (z >= 13 && z < 16) && (x >= 4 && x < S - 4) && (y >= 4 && y < S - 4);
        const bool back = (z >= 15 && z < 28) && (y >= S - 7 && y < S - 4) && (x >= 4 && x < S - 4);
        if (!leg && !seat && !back)
          continue;
        VoxelType t = VOXEL_WOOD_OAK;
        if (seat)
          t = ((x + y) % 3 == 0) ? VOXEL_PLANK : VOXEL_WOOD;
        else if (back && z > 22)
          t = VOXEL_WOOD_PINE;
        else if (leg)
          t = VOXEL_WOOD;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Table: four corner legs and a top slab, inset so the cell reads as furniture not a block.
static void gen_table(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const bool leg = (z < 20) &&
                         ((x >= 3 && x < 6 && y >= 3 && y < 6) ||
                          (x >= S - 6 && x < S - 3 && y >= 3 && y < 6) ||
                          (x >= 3 && x < 6 && y >= S - 6 && y < S - 3) ||
                          (x >= S - 6 && x < S - 3 && y >= S - 6 && y < S - 3));
        const bool top = (z >= 19 && z < 23) && (x >= 2 && x < S - 2) && (y >= 2 && y < S - 2);
        if (!leg && !top)
          continue;
        VoxelType t = VOXEL_PLANK;
        if (top)
        {
          const int grain = (x / 3 + y) % 3;
          t = (grain == 0) ? VOXEL_WOOD_OAK : (grain == 1) ? VOXEL_WOOD : VOXEL_PLANK;
        }
        else
          t = VOXEL_WOOD;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Chest: closed wooden box inset in the cell, with a lid seam and iron banding — solid fill
// (unlike the hollow crate) but air around the box for a prop silhouette.
static void gen_chest(World *w)
{
  const int x0 = 4, x1 = S - 4;
  const int y0 = 6, y1 = S - 6;
  const int z0 = 0, z1 = 18;
  for (int z = z0; z < z1; z++)
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++)
      {
        VoxelType t = VOXEL_WOOD_OAK;
        const int grain = (x + y * 2 + z) % 5;
        if (grain == 0)
          t = VOXEL_WOOD;
        else if (grain == 1)
          t = VOXEL_PLANK;
        // Lid seam near the top.
        if (z == z1 - 4 || z == z1 - 3)
          t = VOXEL_WOOD_PINE;
        // Iron bands / latch.
        if (z == 6 || z == 12 || (z >= 10 && z < 14 && x >= S / 2 - 1 && x <= S / 2 + 1 && y >= y1 - 3))
          t = VOXEL_IRON;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Wooden post-and-rail: posts at Y ends, rails spanning Y in a thin X band (E/W perimeter runs).
static void gen_fence(World *w)
{
  const int x0 = S / 2 - 2, x1 = S / 2 + 2;
  for (int z = 0; z < 26; z++)
    for (int y = 2; y < S - 2; y++)
      for (int x = x0; x < x1; x++)
      {
        const bool post = (y < 6 || y >= S - 6);
        const bool rail = (!post && (z == 8 || z == 14 || z == 20) && x >= x0 + 1 && x < x1 - 1);
        if (!post && !rail)
          continue;
        if (post && z > 24)
          continue;
        VoxelType t = VOXEL_WOOD_OAK;
        if ((x + y + z) % 4 == 0)
          t = VOXEL_WOOD;
        else if (rail && (y % 3 == 0))
          t = VOXEL_PLANK;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Wooden post-and-rail oriented for N/S perimeter runs (rails along X).
static void gen_fence_ns(World *w)
{
  const int y0 = S / 2 - 2, y1 = S / 2 + 2;
  for (int z = 0; z < 26; z++)
    for (int y = y0; y < y1; y++)
      for (int x = 2; x < S - 2; x++)
      {
        const bool post = (x < 6 || x >= S - 6);
        const bool rail = (!post && (z == 8 || z == 14 || z == 20) && y >= y0 + 1 && y < y1 - 1);
        if (!post && !rail)
          continue;
        VoxelType t = VOXEL_WOOD_OAK;
        if ((x + y + z) % 4 == 0)
          t = VOXEL_WOOD;
        else if (rail && (x % 3 == 0))
          t = VOXEL_PLANK;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Woven wattle hurdle: vertical stakes with interlaced horizontal withes.
static void gen_fence_wattle(World *w)
{
  const int x0 = S / 2 - 3, x1 = S / 2 + 3;
  for (int z = 0; z < 28; z++)
    for (int y = 1; y < S - 1; y++)
      for (int x = x0; x < x1; x++)
      {
        const bool stake = ((y % 4) == 0) && x >= x0 + 1 && x < x1 - 1;
        const bool weave = !stake && z >= 4 && z < 24 &&
                           (((z / 2 + y) % 2) == 0) && (x == S / 2 - 1 || x == S / 2);
        if (!stake && !weave)
          continue;
        VoxelType t = stake ? VOXEL_WOOD_HAZELNUT : VOXEL_STRAW;
        if (weave && ((x + y) % 5 == 0))
          t = VOXEL_ROPE;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Iron palings: vertical bars with a top rail, thin X band.
static void gen_fence_iron(World *w)
{
  const int x0 = S / 2 - 1, x1 = S / 2 + 2;
  for (int z = 0; z < 28; z++)
    for (int y = 2; y < S - 2; y++)
      for (int x = x0; x < x1; x++)
      {
        const bool bar = ((y % 3) == 0) && x == S / 2;
        const bool rail = (z >= 24 && z < 27) && x >= x0 && x < x1;
        const bool base = (z < 3) && ((y % 3) == 0) && x == S / 2;
        if (!bar && !rail && !base)
          continue;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_IRON);
      }
}

// Rampart walk: dense stone/cobble fill (sealed solid footpath on wall tops).
static void gen_rampart(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        VoxelType t = VOXEL_COBBLE;
        if (((x * 3 + y * 5 + z) % 7) == 0)
          t = VOXEL_STONE;
        else if (((x + y) % 11) == 0)
          t = VOXEL_BRICK;
        // Slight top wear so the walkway reads as used stone rather than a flat tile.
        if (z >= S - 3 && ((x + y + z) % 5) == 0)
          t = VOXEL_GRAVEL;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// Parapet merlon: solid upright tooth with air around the sides for crenellation rhythm.
static void gen_parapet(World *w)
{
  const int x0 = 6, x1 = S - 6;
  const int y0 = 8, y1 = S - 8;
  for (int z = 0; z < 28; z++)
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++)
      {
        VoxelType t = VOXEL_STONE;
        if (((x + y + z) % 5) == 0)
          t = VOXEL_COBBLE;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

// ---------------------------------------------------------------------------------------------
// Solid construction / craft / geology families
// ---------------------------------------------------------------------------------------------

static void gen_fill_mix3(World *w, VoxelType a, VoxelType b, VoxelType c, float pb, float pc,
                          uint32_t seed)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float r = mat_rand01(x, y, z, seed);
        VoxelType t = a;
        if (r < pc)
          t = c;
        else if (r < pb + pc)
          t = b;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_gravel(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int gx = x / 2, gy = y / 2, gz = z / 2;
        const float g = mat_rand01(gx, gy, gz, 0xA11Cu);
        VoxelType t = VOXEL_GRAVEL;
        if (g < 0.22f)
          t = VOXEL_STONE;
        else if (g < 0.40f)
          t = VOXEL_GRAVEL_BASALT;
        else if (g < 0.55f)
          t = VOXEL_GRAVEL_LIMESTONE;
        else if (g > 0.88f)
          t = VOXEL_SAND;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_brick(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int course = z / 4;
        const int stagger = (course % 2) * 4;
        const int lx = (x + stagger) % 8;
        const int lz = z % 4;
        VoxelType t = VOXEL_BRICK;
        // Mortar is a thin joint, not a full slab — otherwise the underside bakes flat plaster.
        if (lx == 0 || (lz == 0 && (x + y) % 3 != 0))
          t = VOXEL_PLASTER;
        else if ((lx + lz + course) % 5 == 2)
          t = VOXEL_TERRACOTTA;
        else if (mat_rand01(x, y, z, 0xB41Cu) < 0.08f)
          t = VOXEL_ADOBE;
        else if (mat_rand01(x, y, z, 0xB41Du) < 0.05f)
          t = VOXEL_ASH;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}


static void gen_cobble(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        // Irregular stones in a 3D cell lattice so every face (including ±Y) shows mixed colours.
        const int cx = x / 4, cy = y / 4, cz = z / 4;
        const float stone = mat_rand01(cx, cy, cz, 0xC0BAu);
        VoxelType t = VOXEL_COBBLE;
        if (stone < 0.22f)
          t = VOXEL_STONE;
        else if (stone < 0.40f)
          t = VOXEL_STONE_BASALT;
        else if (stone < 0.55f)
          t = VOXEL_STONE_LIMESTONE;
        else if (stone > 0.88f)
          t = VOXEL_GRAVEL;
        // Mortar-ish gaps between stones.
        if ((x % 4) == 0 || (y % 4) == 0 || (z % 4) == 0)
        {
          if (mat_rand01(x, y, z, 0xC0BBu) < 0.55f)
            t = VOXEL_ASH;
        }
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}


static void gen_plaster(World *w)
{
  gen_fill_mix3(w, VOXEL_PLASTER, VOXEL_ASH, VOXEL_SAND, 0.08f, 0.04f, 0xD11Au);
}

static void gen_plank(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int board = x / 5;
        VoxelType t = (board % 2 == 0) ? VOXEL_PLANK : VOXEL_WOOD;
        // Board seams sit inside the run, not on the outer ±X faces (those would bake flat).
        if (x % 5 == 4)
          t = VOXEL_WOOD_OAK;
        else if (mat_rand01(x, y, z, 0xB0ADu) < 0.10f)
          t = VOXEL_WOOD_PINE;
        else if (mat_rand01(x, y, z, 0xB0AEu) < 0.08f)
          t = VOXEL_WOOD_BIRCH;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}


static void gen_straw_bedding(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float strand = mat_noise_2d(x, y + z * 2, 3, 0x57A2u);
        VoxelType t = VOXEL_STRAW;
        if (strand < 0.20f)
          t = VOXEL_WOOL_YELLOW;
        else if (strand > 0.82f)
          t = VOXEL_ASH;
        else if ((x + y) % 7 == 0)
          t = VOXEL_WOOD_PINE;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_wool(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float pile = mat_noise_2d(x * 2, y * 2 + z, 4, 0xF001u);
        VoxelType t = VOXEL_WOOL_WHITE;
        if (pile < 0.25f)
          t = VOXEL_WOOL_GRAY;
        else if (pile > 0.78f)
          t = VOXEL_WOOL;
        else if (mat_rand01(x, y, z, 0xF002u) < 0.05f)
          t = VOXEL_WOOL_BROWN;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_cloth(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const bool warp = ((x / 2) % 2) == 0;
        const bool weft = ((y / 2) % 2) == 0;
        VoxelType t = VOXEL_CLOTH;
        if (warp && weft)
          t = VOXEL_WOOL_WHITE;
        else if (warp)
          t = VOXEL_WOOL_GRAY;
        else if (weft)
          t = VOXEL_WOOL_BROWN;
        else if (mat_rand01(x, y, z, 0xC107u) < 0.08f)
          t = VOXEL_WOOL;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}


static void gen_snow(World *w)
{
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++)
    {
      const int top = S - 1 - (int)(mat_noise_2d(x, y, 8, 0x51A0u) * 2.0f);
      for (int z = 0; z <= top; z++)
      {
        VoxelType t = VOXEL_SNOW;
        if (mat_rand01(x, y, z, 0x51A1u) < 0.07f)
          t = VOXEL_ICE;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
    }
}

static void gen_ice(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        VoxelType t = VOXEL_ICE;
        const float crack = mat_noise_2d(x + z, y - z, 10, 0x1CE0u);
        if (crack > 0.70f)
          t = VOXEL_GLASS_WHITE;
        else if (crack < 0.18f)
          t = VOXEL_SNOW;
        else if (mat_rand01(x, y, z, 0x1CE1u) < 0.10f)
          t = VOXEL_GLASS;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}


static void gen_adobe(World *w)
{
  gen_fill_mix3(w, VOXEL_ADOBE, VOXEL_CLAY, VOXEL_STRAW, 0.12f, 0.08f, 0xAD0Bu);
}

static void gen_clay(World *w)
{
  gen_fill_mix3(w, VOXEL_CLAY, VOXEL_SOIL_CLAY, VOXEL_ADOBE, 0.10f, 0.06f, 0xC1A1u);
}

static void gen_terracotta(World *w)
{
  gen_fill_mix3(w, VOXEL_TERRACOTTA, VOXEL_BRICK, VOXEL_ADOBE, 0.14f, 0.08f, 0x7E44u);
}

static void gen_ceramic(World *w)
{
  gen_fill_mix3(w, VOXEL_CERAMIC, VOXEL_PLASTER, VOXEL_TERRACOTTA, 0.10f, 0.05f, 0xCE4Au);
}

static void gen_ash(World *w)
{
  gen_fill_mix3(w, VOXEL_ASH, VOXEL_STONE_BASALT, VOXEL_SAND, 0.12f, 0.10f, 0xA5A5u);
}

static void gen_ore(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        VoxelType t = VOXEL_STONE;
        const float v = mat_noise_2d(x + z * 2, y - z, 7, 0x0AE1u);
        if (v > 0.72f)
          t = VOXEL_ORE_IRON;
        else if (v > 0.58f)
          t = VOXEL_ORE_COPPER;
        else if (v < 0.18f)
          t = VOXEL_ORE_COAL;
        else if (mat_rand01(x, y, z, 0x0AE2u) < 0.04f)
          t = VOXEL_ORE_GOLD;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_metal(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int band = (y + z / 2) / 3;
        VoxelType t = VOXEL_IRON;
        if (band % 3 == 0)
          t = VOXEL_STEEL;
        else if (band % 3 == 1)
          t = VOXEL_IRON;
        else
          t = VOXEL_COPPER;
        if (mat_rand01(x, y, z, 0xAE7Au) < 0.06f)
          t = VOXEL_SILVER; // highlight
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_crystal(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float facet = mat_noise_2d(x - y, z + x, 5, 0xC895u);
        VoxelType t = VOXEL_CRYSTAL;
        if (facet < 0.25f)
          t = VOXEL_CRYSTAL_BLUE;
        else if (facet < 0.45f)
          t = VOXEL_CRYSTAL_GREEN;
        else if (facet > 0.80f)
          t = VOXEL_CRYSTAL_RED;
        else if (mat_rand01(x, y, z, 0xC897u) < 0.08f)
          t = VOXEL_GLASS_WHITE;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_bone(World *w)
{
  gen_fill_mix3(w, VOXEL_BONE, VOXEL_ASH, VOXEL_PLASTER, 0.10f, 0.06f, 0xB0AEu);
}

static void gen_flesh(World *w)
{
  gen_fill_mix3(w, VOXEL_FLESH, VOXEL_ORGAN, VOXEL_BLOOD, 0.14f, 0.08f, 0xF1E5u);
}

// Fungus: spongy clump with air pockets so it does not read as a solid cube.
static void gen_fungus(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float nx = ((float)x + 0.5f) / (float)S - 0.5f;
        const float ny = ((float)y + 0.5f) / (float)S - 0.5f;
        const float nz = ((float)z + 0.5f) / (float)S - 0.35f;
        if (nx * nx * 2.2f + ny * ny * 2.2f + nz * nz * 1.4f > 0.42f)
          continue;
        if (mat_rand01(x, y, z, 0xF001u) < 0.18f)
          continue; // pore
        VoxelType t = (mat_rand01(x, y, z, 0xF002u) < 0.2f) ? VOXEL_ASH : VOXEL_FUNGUS;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_leather(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const float grain = mat_noise_2d(x, y + z, 6, 0x1EADu);
        VoxelType t = VOXEL_LEATHER;
        if (grain < 0.22f)
          t = VOXEL_WOOL_BROWN;
        else if (grain > 0.80f)
          t = VOXEL_HORN;
        else if (mat_rand01(x, y, z, 0x1EAEu) < 0.10f)
          t = VOXEL_ASH;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}


static void gen_fur(World *w)
{
  gen_fill_mix3(w, VOXEL_FUR, VOXEL_WOOL_BROWN, VOXEL_WOOL_BLACK, 0.18f, 0.10f, 0xF044u);
}

// Feather: fluffy sparse fill — gaps survive the bake.
static void gen_feather(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        if (mat_noise_2d(x + z, y - z, 4, 0xFEA1u) < 0.48f)
          continue;
        VoxelType t = VOXEL_FEATHER;
        if (mat_rand01(x, y, z, 0xFEA2u) < 0.15f)
          t = VOXEL_WOOL_WHITE;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_scale(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int row = y / 3;
        const int stagger = (row % 2) * 2;
        const int lx = (x + stagger) % 5;
        VoxelType t = VOXEL_SCALE;
        if (lx == 0)
          t = VOXEL_STONE_BASALT; // overlap edge
        else if (mat_rand01(x, y, z, 0x5CA1u) < 0.08f)
          t = VOXEL_SHELL;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_shell(World *w)
{
  gen_fill_mix3(w, VOXEL_SHELL, VOXEL_BONE, VOXEL_PLASTER, 0.12f, 0.06f, 0x51E1u);
}

static void gen_horn(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int dx = x - S / 2;
        const int dy = y - S / 2;
        const int ring = (int)(__builtin_sqrtf((float)(dx * dx + dy * dy)));
        VoxelType t = ((ring + z / 3) % 2 == 0) ? VOXEL_HORN : VOXEL_BONE;
        if (mat_rand01(x, y, z, 0x1041u) < 0.05f)
          t = VOXEL_ASH;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_paper(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        VoxelType t = VOXEL_PAPER;
        const int sheet = z / 2;
        if (sheet % 3 == 1)
          t = VOXEL_WOOL_WHITE;
        else if (sheet % 3 == 2)
          t = VOXEL_ASH;
        if (((x + y + z) % 11) == 0)
          t = VOXEL_WOOL_YELLOW;
        else if (mat_rand01(x, y, z, 0x5A11u) < 0.10f)
          t = VOXEL_CLOTH;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}


static void gen_rope(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const int twist = (x + y + z / 2) % 4;
        VoxelType t = VOXEL_ROPE;
        if (twist == 0)
          t = VOXEL_STRAW;
        else if (twist == 2)
          t = VOXEL_WOOL_BROWN;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_wax(World *w)
{
  gen_fill_mix3(w, VOXEL_WAX, VOXEL_WOOL_YELLOW, VOXEL_ASH, 0.10f, 0.04f, 0xA11Au);
}

static void gen_plastic(World *w)
{
  gen_fill_mix3(w, VOXEL_PLASTIC, VOXEL_RUBBER, VOXEL_ASH, 0.08f, 0.03f, 0x51A5u);
}

static void gen_rubber(World *w)
{
  gen_fill_mix3(w, VOXEL_RUBBER, VOXEL_PLASTIC, VOXEL_ASH, 0.10f, 0.04f, 0xABBBu);
}

static void gen_bedrock(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        VoxelType t = VOXEL_BEDROCK;
        const float v = mat_noise_2d(x + z, y - z, 9, 0xBED1u);
        if (v > 0.75f)
          t = VOXEL_STONE_BASALT;
        else if (v < 0.15f)
          t = VOXEL_OBSIDIAN;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
      }
}

static void gen_obsidian(World *w)
{
  gen_fill_mix3(w, VOXEL_OBSIDIAN, VOXEL_STONE_BASALT, VOXEL_GLASS, 0.12f, 0.05f, 0x0B51u);
}

static void gen_limestone_block(World *w)
{
  gen_fill_mix3(w, VOXEL_LIMESTONE, VOXEL_STONE_LIMESTONE, VOXEL_PLASTER, 0.14f, 0.06f, 0x11EEu);
}


// Crafting table: sturdy plank top with thicker legs and a pegboard back.
static void gen_crafting_table(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const bool leg = (z < 18) &&
                         ((x >= 2 && x < 6 && y >= 2 && y < 6) ||
                          (x >= S - 6 && x < S - 2 && y >= 2 && y < 6) ||
                          (x >= 2 && x < 6 && y >= S - 6 && y < S - 2) ||
                          (x >= S - 6 && x < S - 2 && y >= S - 6 && y < S - 2));
        const bool top = (z >= 17 && z < 22) && (x >= 1 && x < S - 1) && (y >= 1 && y < S - 1);
        const bool peg = (z >= 22 && z < 30) && (y >= S - 4 && y < S - 1) && (x >= 4 && x < S - 4) &&
                         ((x + z) % 3 != 0);
        if (!leg && !top && !peg)
          continue;
        VoxelType vt = VOXEL_PLANK;
        if (peg)
          vt = VOXEL_WOOD;
        else if (top)
          vt = ((x / 2 + y) % 2 == 0) ? VOXEL_WOOD_OAK : VOXEL_PLANK;
        else
          vt = VOXEL_WOOD;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, vt);
      }
}

// Anvil: base, waist, and horn of iron/steel.
static void gen_anvil(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const bool base = (z < 6) && (x >= 8 && x < 24) && (y >= 8 && y < 24);
        const bool waist = (z >= 6 && z < 14) && (x >= 12 && x < 20) && (y >= 12 && y < 20);
        const bool body = (z >= 14 && z < 22) && (x >= 6 && x < 26) && (y >= 10 && y < 22);
        const bool horn = (z >= 16 && z < 21) && (x >= 2 && x < 8) && (y >= 13 && y < 19);
        if (!base && !waist && !body && !horn)
          continue;
        VoxelType vt = VOXEL_IRON;
        if (horn || (body && z >= 20))
          vt = VOXEL_STEEL;
        else if (base)
          vt = VOXEL_STONE_BASALT;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, vt);
      }
}

// Forge: brick U-shape with ash bed and a campfire mouth.
static void gen_forge(World *w)
{
  for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++)
      {
        const bool wall = (z < 24) && (
            (x < 5 || x >= S - 5 || y >= S - 5) &&
            !(y < 8 && x >= 10 && x < 22)); // open mouth on -Y
        const bool hearth = (z < 4) && (x >= 6 && x < S - 6) && (y >= 6 && y < S - 6);
        const bool fire = (z >= 4 && z < 10) && (x >= 12 && x < 20) && (y >= 12 && y < 20);
        if (!wall && !hearth && !fire)
          continue;
        VoxelType vt = VOXEL_BRICK;
        if (fire)
          vt = VOXEL_CAMPFIRE;
        else if (hearth)
          vt = VOXEL_ASH;
        else if ((x + y + z) % 7 == 0)
          vt = VOXEL_STONE_BASALT;
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, vt);
      }
}


#undef S

World *material_world_generate(MaterialTemplateKind kind)
{
  if (kind < 0 || kind >= MATERIAL_TEMPLATE_COUNT)
    return NULL;

  World *w = world_create(MATERIAL_WORLD_SIZE, MATERIAL_WORLD_SIZE, MATERIAL_WORLD_SIZE);
  if (!w)
    return NULL;

  switch (kind)
  {
  case MATERIAL_TEMPLATE_GRASS:     gen_grass(w); break;
  case MATERIAL_TEMPLATE_SOIL:      gen_soil(w); break;
  case MATERIAL_TEMPLATE_STONE:     gen_stone(w); break;
  case MATERIAL_TEMPLATE_GRANITE:   gen_granite(w); break;
  case MATERIAL_TEMPLATE_SANDSTONE: gen_sandstone(w); break;
  case MATERIAL_TEMPLATE_WOOD_BARK: gen_wood_bark(w); break;
  case MATERIAL_TEMPLATE_LEAVES:    gen_leaves(w); break;
  case MATERIAL_TEMPLATE_SAND:      gen_sand(w); break;
  case MATERIAL_TEMPLATE_GRASS_TALL: gen_grass_tall(w); break;
  case MATERIAL_TEMPLATE_BUSH:      gen_bush(w); break;
  case MATERIAL_TEMPLATE_THATCH:    gen_thatch(w); break;
  case MATERIAL_TEMPLATE_THATCH_MIRROR: gen_thatch_mirror(w); break;
  case MATERIAL_TEMPLATE_ROOF_TILE: gen_roof_tile(w); break;
  case MATERIAL_TEMPLATE_ROOF_TILE_MIRROR: gen_roof_tile_mirror(w); break;
  case MATERIAL_TEMPLATE_DOOR:      gen_door(w); break;
  case MATERIAL_TEMPLATE_GLASS:     gen_glass(w); break;
  case MATERIAL_TEMPLATE_CRATE:     gen_crate(w); break;
  case MATERIAL_TEMPLATE_BARREL:    gen_barrel(w); break;
  case MATERIAL_TEMPLATE_BED:       gen_bed(w); break;
  case MATERIAL_TEMPLATE_DOOR_NS:   gen_door_ns(w); break;
  case MATERIAL_TEMPLATE_GLASS_NS:  gen_glass_ns(w); break;
  case MATERIAL_TEMPLATE_STAIR:     gen_stair(w); break;
  case MATERIAL_TEMPLATE_STAIR_NS:  gen_stair_ns(w); break;
  case MATERIAL_TEMPLATE_CHAIR:     gen_chair(w); break;
  case MATERIAL_TEMPLATE_TABLE:     gen_table(w); break;
  case MATERIAL_TEMPLATE_CHEST:     gen_chest(w); break;
  case MATERIAL_TEMPLATE_FENCE:     gen_fence(w); break;
  case MATERIAL_TEMPLATE_FENCE_NS:  gen_fence_ns(w); break;
  case MATERIAL_TEMPLATE_FENCE_WATTLE: gen_fence_wattle(w); break;
  case MATERIAL_TEMPLATE_FENCE_IRON: gen_fence_iron(w); break;
  case MATERIAL_TEMPLATE_RAMPART:   gen_rampart(w); break;
  case MATERIAL_TEMPLATE_PARAPET:   gen_parapet(w); break;
  case MATERIAL_TEMPLATE_GRAVEL:    gen_gravel(w); break;
  case MATERIAL_TEMPLATE_BRICK:     gen_brick(w); break;
  case MATERIAL_TEMPLATE_COBBLE:    gen_cobble(w); break;
  case MATERIAL_TEMPLATE_PLASTER:   gen_plaster(w); break;
  case MATERIAL_TEMPLATE_PLANK:     gen_plank(w); break;
  case MATERIAL_TEMPLATE_STRAW:     gen_straw_bedding(w); break;
  case MATERIAL_TEMPLATE_WOOL:      gen_wool(w); break;
  case MATERIAL_TEMPLATE_CLOTH:     gen_cloth(w); break;
  case MATERIAL_TEMPLATE_SNOW:      gen_snow(w); break;
  case MATERIAL_TEMPLATE_ICE:       gen_ice(w); break;
  case MATERIAL_TEMPLATE_ADOBE:     gen_adobe(w); break;
  case MATERIAL_TEMPLATE_CLAY:      gen_clay(w); break;
  case MATERIAL_TEMPLATE_TERRACOTTA: gen_terracotta(w); break;
  case MATERIAL_TEMPLATE_CERAMIC:   gen_ceramic(w); break;
  case MATERIAL_TEMPLATE_ASH:       gen_ash(w); break;
  case MATERIAL_TEMPLATE_ORE:       gen_ore(w); break;
  case MATERIAL_TEMPLATE_METAL:     gen_metal(w); break;
  case MATERIAL_TEMPLATE_CRYSTAL:   gen_crystal(w); break;
  case MATERIAL_TEMPLATE_BONE:      gen_bone(w); break;
  case MATERIAL_TEMPLATE_FLESH:     gen_flesh(w); break;
  case MATERIAL_TEMPLATE_FUNGUS:    gen_fungus(w); break;
  case MATERIAL_TEMPLATE_LEATHER:   gen_leather(w); break;
  case MATERIAL_TEMPLATE_FUR:       gen_fur(w); break;
  case MATERIAL_TEMPLATE_FEATHER:   gen_feather(w); break;
  case MATERIAL_TEMPLATE_SCALE:     gen_scale(w); break;
  case MATERIAL_TEMPLATE_SHELL:     gen_shell(w); break;
  case MATERIAL_TEMPLATE_HORN:      gen_horn(w); break;
  case MATERIAL_TEMPLATE_PAPER:     gen_paper(w); break;
  case MATERIAL_TEMPLATE_ROPE:      gen_rope(w); break;
  case MATERIAL_TEMPLATE_WAX:       gen_wax(w); break;
  case MATERIAL_TEMPLATE_PLASTIC:   gen_plastic(w); break;
  case MATERIAL_TEMPLATE_RUBBER:    gen_rubber(w); break;
  case MATERIAL_TEMPLATE_BEDROCK:   gen_bedrock(w); break;
  case MATERIAL_TEMPLATE_OBSIDIAN:  gen_obsidian(w); break;
  case MATERIAL_TEMPLATE_LIMESTONE_BLOCK: gen_limestone_block(w); break;
  case MATERIAL_TEMPLATE_CRAFTING_TABLE: gen_crafting_table(w); break;
  case MATERIAL_TEMPLATE_ANVIL: gen_anvil(w); break;
  case MATERIAL_TEMPLATE_FORGE: gen_forge(w); break;
  default:                          break;
  }

  return w;
}

// ---------------------------------------------------------------------------------------------
// Baking
// ---------------------------------------------------------------------------------------------

// How far a face texel may look into the material before calling itself empty.
//
// This is what decides whether a material's gaps survive baking. Grass needs the full depth so a
// texel that somehow misses the turf coat still finds the soil host. A canopy needs the opposite —
// leaves are sparse the whole way through, so with an unlimited march almost every texel eventually
// finds a leaf somewhere and the face bakes solid green, losing the sky that makes it read as
// foliage. Tall grass is the mixed case. Its *sides* need the full march: a texel above the tallest
// blade in its row finds nothing however far it looks, which is what makes the silhouette follow
// the grass. Its *top* must not: marching the full depth would paint every blade tip onto the cube
// lid, including blades sitting halfway down the cube, and that flattening is the speckle on
// the top of a tuft. Only blades that actually reach the top of the voxel should occlude a
// downward look; the rest show on the sides.
//
// Ground grass (and stone/sand) also recess their surface, but unlike tall grass they are sealed
// after the march — see seal_side_face_gap — so the air band never stays transparent on a side face.
static int material_bake_max_depth(MaterialTemplateKind kind, MaterialFace face)
{
  switch (kind)
  {
  case MATERIAL_TEMPLATE_LEAVES:
    return 8; // only the outer shell of the canopy; deeper gaps stay open
  case MATERIAL_TEMPLATE_BUSH:
    return 10; // denser than leaves, still short of a solid fill
  case MATERIAL_TEMPLATE_GRASS_TALL:
    if (face == MATERIAL_FACE_TOP)
      return 12; // only the upper canopy; recessed blades stay off the lid
    return MATERIAL_WORLD_SIZE;
  case MATERIAL_TEMPLATE_FUNGUS:
    return 10;
  case MATERIAL_TEMPLATE_FEATHER:
    return 8;
  default:
    return MATERIAL_WORLD_SIZE;
  }
}

static uint32_t pack_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
  return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// Map a face texel plus a depth along the inward axis to a sub-voxel coordinate.
//
// The (u,v) parameterisation of each face is chosen so that the baked texture lines up with the
// way the renderer lays out that face's quad, and so that depth always increases *into* the cube.
// The three faces the isometric camera cannot see mirror their opposite along u, which is what an
// eye moved round to the other side of the cube would find there.
static void face_sample_coord(MaterialFace face, int u, int v, int depth, int *x, int *y, int *z)
{
  const int last = MATERIAL_WORLD_SIZE - 1;
  switch (face)
  {
  case MATERIAL_FACE_TOP: // looking down -Z at the +Z face
    *x = u;
    *y = v;
    *z = last - depth;
    break;
  case MATERIAL_FACE_LEFT: // looking along -Y at the +Y face
    *x = u;
    *y = last - depth;
    *z = last - v;
    break;
  case MATERIAL_FACE_RIGHT: // looking along -X at the +X face
    *x = last - depth;
    *y = u;
    *z = last - v;
    break;
  case MATERIAL_FACE_BOTTOM: // looking up +Z at the -Z face
    *x = u;
    *y = last - v;
    *z = depth;
    break;
  case MATERIAL_FACE_BACK: // looking along +Y at the -Y face
    *x = last - u;
    *y = depth;
    *z = last - v;
    break;
  case MATERIAL_FACE_FRONT: // looking along +X at the -X face
    *x = depth;
    *y = last - u;
    *z = last - v;
    break;
  default:
    *x = *y = *z = 0;
    break;
  }
}

MaterialFace material_face_for_normal(int axis, bool positive)
{
  switch (axis)
  {
  case 0:
    return positive ? MATERIAL_FACE_RIGHT : MATERIAL_FACE_FRONT;
  case 1:
    return positive ? MATERIAL_FACE_LEFT : MATERIAL_FACE_BACK;
  default:
    return positive ? MATERIAL_FACE_TOP : MATERIAL_FACE_BOTTOM;
  }
}

// Pack a shaded albedo. `depth` is how many empty steps were skipped before the hit — crevice
// self-shadowing, not a light.
static uint32_t bake_shaded_texel(VoxelType type, int depth)
{
  uint8_t r, g, b;
  world_voxel_type_color(type, &r, &g, &b);
  const int shade_steps = depth < 6 ? depth : 6;
  const float shade = 1.0f - (float)shade_steps * 0.085f;
  r = (uint8_t)((float)r * shade);
  g = (uint8_t)((float)g * shade);
  b = (uint8_t)((float)b * shade);
  return pack_argb(255, r, g, b);
}

// When a side-face ray finds only air, paint the column's surface colour instead of leaving a hole.
//
// Ground materials (grass, stone, sand) may recess their surface below the cube lid so the top
// face can show relief — a thin turf coat, pitted stone. Looking sideways into that air band used
// to bake transparent, and with atlas blending those texels punched through to the parent voxels
// under the grass. Sparse materials (leaves, tall grass) skip this: their holes are the point.
static uint32_t seal_side_face_gap(const World *world, MaterialFace face, int u, int v)
{
  if (face == MATERIAL_FACE_TOP || face == MATERIAL_FACE_BOTTOM)
    return 0;

  int x, y, z;
  face_sample_coord(face, u, v, 0, &x, &y, &z);
  for (int zz = z; zz >= 0; zz--)
  {
    const Voxel *sv = world_voxel_cptr_fast(world, x, y, zz);
    if (!sv || sv->type == VOXEL_AIR)
      continue;
    // Shade by how far above the surface this empty face texel sits, so the sealed band still
    // reads as "above the dirt" rather than a flat repaint of the tip colour.
    return bake_shaded_texel(sv->type, z - zz);
  }
  return 0;
}

static bool bake_faces_with_depths(const World *world, const int *max_depth_per_face,
                                   bool seal_empty, MaterialFaceBake *out)
{
  if (!world || !world->voxels || !out || !max_depth_per_face)
    return false;
  if (world->width != MATERIAL_WORLD_SIZE || world->height != MATERIAL_WORLD_SIZE ||
      world->depth != MATERIAL_WORLD_SIZE)
    return false;

  memset(out, 0, sizeof(*out));

  for (int face = 0; face < MATERIAL_FACE_COUNT; face++)
  {
    int max_depth = max_depth_per_face[face];
    if (max_depth < 1)
      return false;
    if (max_depth > MATERIAL_WORLD_SIZE)
      max_depth = MATERIAL_WORLD_SIZE;

    for (int v = 0; v < MATERIAL_FACE_SIZE; v++)
    {
      for (int u = 0; u < MATERIAL_FACE_SIZE; u++)
      {
        uint32_t packed = 0; // transparent until something solid is found

        for (int depth = 0; depth < max_depth; depth++)
        {
          int x, y, z;
          face_sample_coord((MaterialFace)face, u, v, depth, &x, &y, &z);
          const Voxel *sv = world_voxel_cptr_fast(world, x, y, z);
          if (!sv || sv->type == VOXEL_AIR)
            continue;

          packed = bake_shaded_texel(sv->type, depth);
          break;
        }

        // Ground materials must stay opaque on every face of the parent cube. Leaving the air
        // above a recessed surface transparent is what let terrain under grass show through.
        if (packed == 0 && seal_empty)
          packed = seal_side_face_gap(world, (MaterialFace)face, u, v);

        out->texels[face][v * MATERIAL_FACE_SIZE + u] = packed;
      }
    }
  }

  return true;
}

bool material_world_bake_faces_depth(const World *world, int max_depth, MaterialFaceBake *out)
{
  int depths[MATERIAL_FACE_COUNT];
  for (int f = 0; f < MATERIAL_FACE_COUNT; f++)
    depths[f] = max_depth;
  // Public bake helpers have no material kind, so they cannot tell sparse from solid; leave gaps
  // alone and let material_worlds_init apply sealing per template.
  return bake_faces_with_depths(world, depths, false, out);
}

bool material_world_bake_faces(const World *world, MaterialFaceBake *out)
{
  return material_world_bake_faces_depth(world, MATERIAL_WORLD_SIZE, out);
}

// ---------------------------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------------------------

static MaterialTemplate g_templates[MATERIAL_TEMPLATE_COUNT];
static bool g_initialised = false;

// Templates whose bake must leave transparent texels where the nested world is air. Keep in sync
// with world_voxel_type_has_material_gaps for the VoxelTypes that map here.
static bool material_template_kind_keeps_gaps(MaterialTemplateKind kind)
{
  switch (kind)
  {
  case MATERIAL_TEMPLATE_LEAVES:
  case MATERIAL_TEMPLATE_GRASS_TALL:
  case MATERIAL_TEMPLATE_BUSH:
  case MATERIAL_TEMPLATE_THATCH:
  case MATERIAL_TEMPLATE_THATCH_MIRROR:
  case MATERIAL_TEMPLATE_ROOF_TILE:
  case MATERIAL_TEMPLATE_ROOF_TILE_MIRROR:
  case MATERIAL_TEMPLATE_DOOR:
  case MATERIAL_TEMPLATE_DOOR_NS:
  case MATERIAL_TEMPLATE_GLASS:
  case MATERIAL_TEMPLATE_GLASS_NS:
  case MATERIAL_TEMPLATE_CRATE:
  case MATERIAL_TEMPLATE_BARREL:
  case MATERIAL_TEMPLATE_BED:
  case MATERIAL_TEMPLATE_STAIR:
  case MATERIAL_TEMPLATE_STAIR_NS:
  case MATERIAL_TEMPLATE_CHAIR:
  case MATERIAL_TEMPLATE_TABLE:
  case MATERIAL_TEMPLATE_CHEST:
  case MATERIAL_TEMPLATE_FENCE:
  case MATERIAL_TEMPLATE_FENCE_NS:
  case MATERIAL_TEMPLATE_FENCE_WATTLE:
  case MATERIAL_TEMPLATE_FENCE_IRON:
  case MATERIAL_TEMPLATE_CRAFTING_TABLE:
  case MATERIAL_TEMPLATE_ANVIL:
  case MATERIAL_TEMPLATE_FORGE:
  case MATERIAL_TEMPLATE_PARAPET:
  case MATERIAL_TEMPLATE_FUNGUS:
  case MATERIAL_TEMPLATE_FEATHER:
    return true;
  default:
    return false;
  }
}

bool material_template_kind_is_sparse(MaterialTemplateKind kind)
{
  return material_template_kind_keeps_gaps(kind);
}

bool material_worlds_init(void)
{
  if (g_initialised)
    return true;

  for (int i = 0; i < MATERIAL_TEMPLATE_COUNT; i++)
  {
    g_templates[i].kind = (MaterialTemplateKind)i;
    g_templates[i].world = material_world_generate((MaterialTemplateKind)i);
    if (!g_templates[i].world)
    {
      material_worlds_shutdown();
      return false;
    }
    int depths[MATERIAL_FACE_COUNT];
    for (int f = 0; f < MATERIAL_FACE_COUNT; f++)
      depths[f] = material_bake_max_depth((MaterialTemplateKind)i, (MaterialFace)f);
    // Sparse surface materials keep transparent gaps so angled roofs, panes, furniture, and
    // foliage show their silhouette instead of sealing to a full cube.
    const bool seal_empty = !material_template_kind_keeps_gaps((MaterialTemplateKind)i);
    g_templates[i].baked =
        bake_faces_with_depths(g_templates[i].world, depths, seal_empty, &g_templates[i].bake);
    if (!g_templates[i].baked)
    {
      material_worlds_shutdown();
      return false;
    }
  }

  g_initialised = true;
  return true;
}

void material_worlds_shutdown(void)
{
  for (int i = 0; i < MATERIAL_TEMPLATE_COUNT; i++)
  {
    if (g_templates[i].world)
    {
      world_destroy(g_templates[i].world);
      g_templates[i].world = NULL;
    }
    g_templates[i].baked = false;
  }
  g_initialised = false;
}

const MaterialTemplate *material_worlds_get(MaterialTemplateKind kind)
{
  if (!g_initialised || kind < 0 || kind >= MATERIAL_TEMPLATE_COUNT)
    return NULL;
  return &g_templates[kind];
}

const MaterialTemplate *material_worlds_for_voxel(VoxelType type)
{
  if (!g_initialised)
    return NULL;

  // Whole type families share one template: the game has eighteen woods and they are all bark from
  // a sub-voxel's point of view. Species differences live in the parent voxel's colour, which the
  // renderer still applies on top.
  if (type >= VOXEL_WOOD && type <= VOXEL_WOOD_REDWOOD)
    return &g_templates[MATERIAL_TEMPLATE_WOOD_BARK];
  // Through REDWOOD, the last of the nineteen. This used to stop at PINE, which is the fifth, so
  // fourteen species of leaf had no template and drew as flat colour next to textured neighbours.
  if (type >= VOXEL_LEAVES && type <= VOXEL_LEAVES_REDWOOD)
    return &g_templates[MATERIAL_TEMPLATE_LEAVES];
  if (voxel_type_is_bush(type))
    return &g_templates[MATERIAL_TEMPLATE_BUSH];

  switch (type)
  {
  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
  case VOXEL_GRASS_MOSS:
    return &g_templates[MATERIAL_TEMPLATE_GRASS];

  case VOXEL_GRASS_TALL:
    return &g_templates[MATERIAL_TEMPLATE_GRASS_TALL];

  case VOXEL_SOIL:
  case VOXEL_SOIL_CLAY:
  case VOXEL_SOIL_LOAM:
  case VOXEL_SOIL_SILT:
    return &g_templates[MATERIAL_TEMPLATE_SOIL];

  case VOXEL_STONE_GRANITE:
  case VOXEL_GRAVEL_GRANITE:
    return &g_templates[MATERIAL_TEMPLATE_GRANITE];

  case VOXEL_STONE_SANDSTONE:
  case VOXEL_GRAVEL_SANDSTONE:
    return &g_templates[MATERIAL_TEMPLATE_SANDSTONE];

  case VOXEL_SAND:
  case VOXEL_SAND_BASALT:
  case VOXEL_SAND_GRANITE:
  case VOXEL_SAND_LIMESTONE:
  case VOXEL_SAND_SANDSTONE:
    return &g_templates[MATERIAL_TEMPLATE_SAND];

  case VOXEL_GRAVEL:
    return &g_templates[MATERIAL_TEMPLATE_GRAVEL];

  case VOXEL_THATCH:
    return &g_templates[MATERIAL_TEMPLATE_THATCH];
  case VOXEL_THATCH_MIRROR:
    return &g_templates[MATERIAL_TEMPLATE_THATCH_MIRROR];

  case VOXEL_ROOF_TILE:
    return &g_templates[MATERIAL_TEMPLATE_ROOF_TILE];
  case VOXEL_ROOF_TILE_MIRROR:
    return &g_templates[MATERIAL_TEMPLATE_ROOF_TILE_MIRROR];

  case VOXEL_DOOR:
    return &g_templates[MATERIAL_TEMPLATE_DOOR];
  case VOXEL_DOOR_NS:
    return &g_templates[MATERIAL_TEMPLATE_DOOR_NS];

  case VOXEL_GLASS:
  case VOXEL_GLASS_WHITE:
  case VOXEL_GLASS_RED:
  case VOXEL_GLASS_GREEN:
  case VOXEL_GLASS_BLUE:
  case VOXEL_GLASS_YELLOW:
    return &g_templates[MATERIAL_TEMPLATE_GLASS];
  case VOXEL_GLASS_NS:
    return &g_templates[MATERIAL_TEMPLATE_GLASS_NS];

  case VOXEL_CRATE:
    return &g_templates[MATERIAL_TEMPLATE_CRATE];
  case VOXEL_BARREL:
    return &g_templates[MATERIAL_TEMPLATE_BARREL];
  case VOXEL_BED:
    return &g_templates[MATERIAL_TEMPLATE_BED];

  case VOXEL_STAIR:
    return &g_templates[MATERIAL_TEMPLATE_STAIR];
  case VOXEL_STAIR_NS:
    return &g_templates[MATERIAL_TEMPLATE_STAIR_NS];
  case VOXEL_CHAIR:
    return &g_templates[MATERIAL_TEMPLATE_CHAIR];
  case VOXEL_TABLE:
    return &g_templates[MATERIAL_TEMPLATE_TABLE];
  case VOXEL_CHEST:
    return &g_templates[MATERIAL_TEMPLATE_CHEST];
  case VOXEL_FENCE:
    return &g_templates[MATERIAL_TEMPLATE_FENCE];
  case VOXEL_FENCE_NS:
    return &g_templates[MATERIAL_TEMPLATE_FENCE_NS];
  case VOXEL_FENCE_WATTLE:
    return &g_templates[MATERIAL_TEMPLATE_FENCE_WATTLE];
  case VOXEL_FENCE_IRON:
    return &g_templates[MATERIAL_TEMPLATE_FENCE_IRON];
  case VOXEL_RAMPART:
    return &g_templates[MATERIAL_TEMPLATE_RAMPART];
  case VOXEL_PARAPET:
    return &g_templates[MATERIAL_TEMPLATE_PARAPET];
  case VOXEL_CRAFTING_TABLE:
    return &g_templates[MATERIAL_TEMPLATE_CRAFTING_TABLE];
  case VOXEL_ANVIL:
    return &g_templates[MATERIAL_TEMPLATE_ANVIL];
  case VOXEL_FORGE:
    return &g_templates[MATERIAL_TEMPLATE_FORGE];

  case VOXEL_BRICK:
    return &g_templates[MATERIAL_TEMPLATE_BRICK];
  case VOXEL_COBBLE:
    return &g_templates[MATERIAL_TEMPLATE_COBBLE];
  case VOXEL_PLASTER:
    return &g_templates[MATERIAL_TEMPLATE_PLASTER];
  case VOXEL_PLANK:
    return &g_templates[MATERIAL_TEMPLATE_PLANK];
  case VOXEL_STRAW:
    return &g_templates[MATERIAL_TEMPLATE_STRAW];
  case VOXEL_WOOL:
  case VOXEL_WOOL_WHITE:
  case VOXEL_WOOL_BLACK:
  case VOXEL_WOOL_BROWN:
  case VOXEL_WOOL_GRAY:
  case VOXEL_WOOL_RED:
  case VOXEL_WOOL_BLUE:
  case VOXEL_WOOL_GREEN:
  case VOXEL_WOOL_YELLOW:
    return &g_templates[MATERIAL_TEMPLATE_WOOL];
  case VOXEL_CLOTH:
    return &g_templates[MATERIAL_TEMPLATE_CLOTH];
  case VOXEL_SNOW:
    return &g_templates[MATERIAL_TEMPLATE_SNOW];
  case VOXEL_ICE:
    return &g_templates[MATERIAL_TEMPLATE_ICE];
  case VOXEL_ADOBE:
    return &g_templates[MATERIAL_TEMPLATE_ADOBE];
  case VOXEL_CLAY:
    return &g_templates[MATERIAL_TEMPLATE_CLAY];
  case VOXEL_TERRACOTTA:
    return &g_templates[MATERIAL_TEMPLATE_TERRACOTTA];
  case VOXEL_CERAMIC:
    return &g_templates[MATERIAL_TEMPLATE_CERAMIC];
  case VOXEL_ASH:
    return &g_templates[MATERIAL_TEMPLATE_ASH];
  case VOXEL_ORE:
  case VOXEL_ORE_COAL:
  case VOXEL_ORE_ADAMANTITE:
  case VOXEL_ORE_HEMATITE:
  case VOXEL_ORE_MITHRIL:
  case VOXEL_ORE_COPPER:
  case VOXEL_ORE_SILVER:
  case VOXEL_ORE_GOLD:
  case VOXEL_ORE_TIN:
  case VOXEL_ORE_IRON:
  case VOXEL_ORE_LEAD:
  case VOXEL_ORE_ZINC:
  case VOXEL_ORE_TITANIUM:
  case VOXEL_ORE_ALUMINUM:
  case VOXEL_ORE_MAGNESIUM:
  case VOXEL_ORE_COBALT:
  case VOXEL_ORE_NICKEL:
  case VOXEL_ORE_PLATINUM:
    return &g_templates[MATERIAL_TEMPLATE_ORE];
  case VOXEL_ADAMANTITE:
  case VOXEL_HEMATITE:
  case VOXEL_MITHRIL:
  case VOXEL_COPPER:
  case VOXEL_SILVER:
  case VOXEL_GOLD:
  case VOXEL_TIN:
  case VOXEL_IRON:
  case VOXEL_LEAD:
  case VOXEL_ZINC:
  case VOXEL_STEEL:
  case VOXEL_TITANIUM:
  case VOXEL_ALUMINUM:
  case VOXEL_MAGNESIUM:
  case VOXEL_COBALT:
  case VOXEL_NICKEL:
  case VOXEL_PLATINUM:
    return &g_templates[MATERIAL_TEMPLATE_METAL];
  case VOXEL_CRYSTAL:
  case VOXEL_CRYSTAL_RED:
  case VOXEL_CRYSTAL_GREEN:
  case VOXEL_CRYSTAL_BLUE:
    return &g_templates[MATERIAL_TEMPLATE_CRYSTAL];
  case VOXEL_BONE:
    return &g_templates[MATERIAL_TEMPLATE_BONE];
  case VOXEL_FLESH:
  case VOXEL_ORGAN:
  case VOXEL_BLOOD:
  case VOXEL_BRAIN:
    return &g_templates[MATERIAL_TEMPLATE_FLESH];
  case VOXEL_FUNGUS:
    return &g_templates[MATERIAL_TEMPLATE_FUNGUS];
  case VOXEL_LEATHER:
    return &g_templates[MATERIAL_TEMPLATE_LEATHER];
  case VOXEL_FUR:
    return &g_templates[MATERIAL_TEMPLATE_FUR];
  case VOXEL_FEATHER:
    return &g_templates[MATERIAL_TEMPLATE_FEATHER];
  case VOXEL_SCALE:
    return &g_templates[MATERIAL_TEMPLATE_SCALE];
  case VOXEL_SHELL:
    return &g_templates[MATERIAL_TEMPLATE_SHELL];
  case VOXEL_HORN:
    return &g_templates[MATERIAL_TEMPLATE_HORN];
  case VOXEL_PAPER:
    return &g_templates[MATERIAL_TEMPLATE_PAPER];
  case VOXEL_ROPE:
    return &g_templates[MATERIAL_TEMPLATE_ROPE];
  case VOXEL_WAX:
    return &g_templates[MATERIAL_TEMPLATE_WAX];
  case VOXEL_PLASTIC:
    return &g_templates[MATERIAL_TEMPLATE_PLASTIC];
  case VOXEL_RUBBER:
    return &g_templates[MATERIAL_TEMPLATE_RUBBER];
  case VOXEL_BEDROCK:
    return &g_templates[MATERIAL_TEMPLATE_BEDROCK];
  case VOXEL_OBSIDIAN:
    return &g_templates[MATERIAL_TEMPLATE_OBSIDIAN];
  case VOXEL_LIMESTONE:
    return &g_templates[MATERIAL_TEMPLATE_LIMESTONE_BLOCK];

  case VOXEL_STONE:
  case VOXEL_STONE_BASALT:
  case VOXEL_STONE_LIMESTONE:
  case VOXEL_GRAVEL_BASALT:
  case VOXEL_GRAVEL_LIMESTONE:
    return &g_templates[MATERIAL_TEMPLATE_STONE];

  default:
    return NULL; // no template yet; the renderer falls back to a flat face
  }
}

bool material_worlds_sample(VoxelType type, MaterialFace face, float u, float v,
                            uint8_t *r, uint8_t *g, uint8_t *b)
{
  if (face < 0 || face >= MATERIAL_FACE_COUNT)
    return false;

  const MaterialTemplate *t = material_worlds_for_voxel(type);
  if (!t || !t->baked)
    return false;

  // Wrap rather than clamp. A caller walking across a run of voxels passes a coordinate that keeps
  // growing, and one tile per voxel is what keeps the grain aligned to the lattice.
  const float fu = u - floorf(u);
  const float fv = v - floorf(v);
  int tu = (int)(fu * (float)MATERIAL_FACE_SIZE);
  int tv = (int)(fv * (float)MATERIAL_FACE_SIZE);
  if (tu < 0)
    tu = 0;
  else if (tu >= MATERIAL_FACE_SIZE)
    tu = MATERIAL_FACE_SIZE - 1;
  if (tv < 0)
    tv = 0;
  else if (tv >= MATERIAL_FACE_SIZE)
    tv = MATERIAL_FACE_SIZE - 1;

  const uint32_t texel = t->bake.texels[face][tv * MATERIAL_FACE_SIZE + tu];
  if ((texel >> 24) == 0)
    return false; // a gap in the material, not a colour

  if (r)
    *r = (uint8_t)((texel >> 16) & 0xFFu);
  if (g)
    *g = (uint8_t)((texel >> 8) & 0xFFu);
  if (b)
    *b = (uint8_t)(texel & 0xFFu);
  return true;
}

const char *material_template_name(MaterialTemplateKind kind)
{
  switch (kind)
  {
  case MATERIAL_TEMPLATE_GRASS:     return "grass";
  case MATERIAL_TEMPLATE_SOIL:      return "soil";
  case MATERIAL_TEMPLATE_STONE:     return "stone";
  case MATERIAL_TEMPLATE_GRANITE:   return "granite";
  case MATERIAL_TEMPLATE_SANDSTONE: return "sandstone";
  case MATERIAL_TEMPLATE_WOOD_BARK: return "wood bark";
  case MATERIAL_TEMPLATE_LEAVES:    return "leaves";
  case MATERIAL_TEMPLATE_SAND:      return "sand";
  case MATERIAL_TEMPLATE_GRASS_TALL: return "tall grass";
  case MATERIAL_TEMPLATE_BUSH:      return "bush";
  case MATERIAL_TEMPLATE_THATCH:    return "thatch";
  case MATERIAL_TEMPLATE_THATCH_MIRROR: return "thatch mirror";
  case MATERIAL_TEMPLATE_ROOF_TILE: return "roof tile";
  case MATERIAL_TEMPLATE_ROOF_TILE_MIRROR: return "roof tile mirror";
  case MATERIAL_TEMPLATE_DOOR:      return "door";
  case MATERIAL_TEMPLATE_GLASS:     return "glass";
  case MATERIAL_TEMPLATE_CRATE:     return "crate";
  case MATERIAL_TEMPLATE_BARREL:    return "barrel";
  case MATERIAL_TEMPLATE_BED:       return "bed";
  case MATERIAL_TEMPLATE_DOOR_NS:   return "door ns";
  case MATERIAL_TEMPLATE_GLASS_NS:  return "glass ns";
  case MATERIAL_TEMPLATE_STAIR:     return "stair";
  case MATERIAL_TEMPLATE_STAIR_NS:  return "stair ns";
  case MATERIAL_TEMPLATE_CHAIR:     return "chair";
  case MATERIAL_TEMPLATE_TABLE:     return "table";
  case MATERIAL_TEMPLATE_CHEST:     return "chest";
  case MATERIAL_TEMPLATE_FENCE:     return "fence";
  case MATERIAL_TEMPLATE_FENCE_NS:  return "fence ns";
  case MATERIAL_TEMPLATE_FENCE_WATTLE: return "fence wattle";
  case MATERIAL_TEMPLATE_FENCE_IRON: return "fence iron";
  case MATERIAL_TEMPLATE_RAMPART:   return "rampart";
  case MATERIAL_TEMPLATE_PARAPET:   return "parapet";
  case MATERIAL_TEMPLATE_GRAVEL:    return "gravel";
  case MATERIAL_TEMPLATE_BRICK:     return "brick";
  case MATERIAL_TEMPLATE_COBBLE:    return "cobble";
  case MATERIAL_TEMPLATE_PLASTER:   return "plaster";
  case MATERIAL_TEMPLATE_PLANK:     return "plank";
  case MATERIAL_TEMPLATE_STRAW:     return "straw";
  case MATERIAL_TEMPLATE_WOOL:      return "wool";
  case MATERIAL_TEMPLATE_CLOTH:     return "cloth";
  case MATERIAL_TEMPLATE_SNOW:      return "snow";
  case MATERIAL_TEMPLATE_ICE:       return "ice";
  case MATERIAL_TEMPLATE_ADOBE:     return "adobe";
  case MATERIAL_TEMPLATE_CLAY:      return "clay";
  case MATERIAL_TEMPLATE_TERRACOTTA: return "terracotta";
  case MATERIAL_TEMPLATE_CERAMIC:   return "ceramic";
  case MATERIAL_TEMPLATE_ASH:       return "ash";
  case MATERIAL_TEMPLATE_ORE:       return "ore";
  case MATERIAL_TEMPLATE_METAL:     return "metal";
  case MATERIAL_TEMPLATE_CRYSTAL:   return "crystal";
  case MATERIAL_TEMPLATE_BONE:      return "bone";
  case MATERIAL_TEMPLATE_FLESH:     return "flesh";
  case MATERIAL_TEMPLATE_FUNGUS:    return "fungus";
  case MATERIAL_TEMPLATE_LEATHER:   return "leather";
  case MATERIAL_TEMPLATE_FUR:       return "fur";
  case MATERIAL_TEMPLATE_FEATHER:   return "feather";
  case MATERIAL_TEMPLATE_SCALE:     return "scale";
  case MATERIAL_TEMPLATE_SHELL:     return "shell";
  case MATERIAL_TEMPLATE_HORN:      return "horn";
  case MATERIAL_TEMPLATE_PAPER:     return "paper";
  case MATERIAL_TEMPLATE_ROPE:      return "rope";
  case MATERIAL_TEMPLATE_WAX:       return "wax";
  case MATERIAL_TEMPLATE_PLASTIC:   return "plastic";
  case MATERIAL_TEMPLATE_RUBBER:    return "rubber";
  case MATERIAL_TEMPLATE_BEDROCK:   return "bedrock";
  case MATERIAL_TEMPLATE_OBSIDIAN:  return "obsidian";
  case MATERIAL_TEMPLATE_LIMESTONE_BLOCK: return "limestone block";
  case MATERIAL_TEMPLATE_COUNT:
  default:                          return "unknown";
  }
}

bool material_worlds_is_sparse(VoxelType type)
{
  return world_voxel_type_has_material_gaps(type);
}

const char *material_face_name(MaterialFace face)
{
  switch (face)
  {
  case MATERIAL_FACE_TOP:    return "top";
  case MATERIAL_FACE_LEFT:   return "left";
  case MATERIAL_FACE_RIGHT:  return "right";
  case MATERIAL_FACE_BOTTOM: return "bottom";
  case MATERIAL_FACE_BACK:   return "back";
  case MATERIAL_FACE_FRONT:  return "front";
  default:                   return "unknown";
  }
}
