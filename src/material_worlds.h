#ifndef MATERIAL_WORLDS_H
#define MATERIAL_WORLDS_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel.h"
#include "world.h"

// Sub-voxel rendering: material detail as nested worlds.
//
// A voxel in the game world is one unit of material. What that material *looks* like is described
// here by a second, nested world of 32x32x32 sub-voxels — a "material template world". Low grass is
// a thin turf coat on a soil host, bark is fibres running along the trunk, sandstone is bedding
// planes, granite is mineral grain. Each is real voxel data in an ordinary World, generated the
// same deterministic way the terrain is, so the same tools that inspect a game world can inspect
// these.
//
// Drawing 32^3 sub-voxels for every parent voxel on screen is not something a renderer can do
// directly: at 13k visible voxels that would be 430 million sub-voxels a frame. So each template is
// *baked* once into the three faces an isometric camera can see, at one texel per surface sub-voxel
// (32x32 each). The renderer then draws a voxel face as a textured quad instead of a flat diamond,
// and the sub-voxel structure shows up at no per-frame cost. The nested world remains the source of
// truth, which is what makes the higher-detail rendering paths — ray-marching a parent voxel,
// mip-chains per face, deformation — additions rather than rewrites.
//
// Templates are a fixed, shared set: every grass voxel in the universe uses the same nested world,
// so the memory cost is per material, not per voxel.

#define MATERIAL_WORLD_SIZE 32 // sub-voxels along each axis of a parent voxel
#define MATERIAL_FACE_SIZE 32  // baked face texture is one texel per surface sub-voxel

// Low-grass coat thickness inside MATERIAL_TEMPLATE_GRASS.
//
// A VOXEL_GRASS* parent is a soil host wearing this coat: the nested world is soil fill with only
// the top MATERIAL_GRASS_COAT_DEPTH sub-voxels green. That matches sod depth (~6% of the parent),
// keeps the top face an opaque lawn, and paints a two-texel green rim on side faces so cliffs read
// as dirt with turf rather than a full cube of blades. Breaking the parent scrapes the coat and
// leaves soil (see voxel_type_is_low_grass).
#define MATERIAL_GRASS_COAT_DEPTH 2

// The materials with a template so far. Deliberately the basic surfaces the game shows most:
// terrain the player walks on, and the trees and rock they walk past.
typedef enum
{
  MATERIAL_TEMPLATE_GRASS = 0, // soil host + MATERIAL_GRASS_COAT_DEPTH turf coat
  MATERIAL_TEMPLATE_SOIL,
  MATERIAL_TEMPLATE_STONE,
  MATERIAL_TEMPLATE_GRANITE,
  MATERIAL_TEMPLATE_SANDSTONE,
  MATERIAL_TEMPLATE_WOOD_BARK,
  MATERIAL_TEMPLATE_LEAVES,
  MATERIAL_TEMPLATE_SAND,
  // Tall grass: blades standing in air rather than in soil, so most of the cube is empty and bakes
  // transparent. Distinct from MATERIAL_TEMPLATE_GRASS (ground turf on a dirt host).
  MATERIAL_TEMPLATE_GRASS_TALL,
  // Bushes: denser leafy clumps than canopy leaves, still with gaps so the shrub is not a solid cube.
  MATERIAL_TEMPLATE_BUSH,
  // Settlement fittings: thin sub-voxel surfaces (angled roof shells, hung door, glass pane).
  // These bake with transparent gaps so the silhouette follows the surface rather than a full cube.
  MATERIAL_TEMPLATE_THATCH,           // thatch shell sloping up along +X
  MATERIAL_TEMPLATE_THATCH_MIRROR,    // thatch shell sloping up along -X (right roof half)
  MATERIAL_TEMPLATE_ROOF_TILE,        // clay shingle shell sloping up along +X
  MATERIAL_TEMPLATE_ROOF_TILE_MIRROR, // clay shingle shell sloping up along -X
  MATERIAL_TEMPLATE_DOOR,             // thin boarded door panel (X mid-plane, E/W walls)
  MATERIAL_TEMPLATE_GLASS,            // thin window pane (X mid-plane, E/W walls)
  MATERIAL_TEMPLATE_CRATE,            // hollow wooden crate
  MATERIAL_TEMPLATE_BARREL,           // cylindrical barrel shell
  MATERIAL_TEMPLATE_BED,              // straw/wool mattress on a plank frame
  MATERIAL_TEMPLATE_DOOR_NS,          // door panel in the Y mid-plane (N/S walls)
  MATERIAL_TEMPLATE_GLASS_NS,         // glass pane in the Y mid-plane (N/S walls)
  MATERIAL_TEMPLATE_STAIR,            // stepped wedge rising along +X
  MATERIAL_TEMPLATE_STAIR_NS,         // stepped wedge rising along +Y
  MATERIAL_TEMPLATE_CHAIR,            // seat + backrest
  MATERIAL_TEMPLATE_TABLE,            // four legs + top slab
  MATERIAL_TEMPLATE_CHEST,            // closed inset storage chest
  MATERIAL_TEMPLATE_FENCE,            // wooden post-and-rail (rails along Y)
  MATERIAL_TEMPLATE_FENCE_NS,         // wooden post-and-rail (rails along X)
  MATERIAL_TEMPLATE_FENCE_WATTLE,     // woven brush / hurdle
  MATERIAL_TEMPLATE_FENCE_IRON,       // iron palings
  MATERIAL_TEMPLATE_RAMPART,          // stone walkway slab for wall tops
  MATERIAL_TEMPLATE_PARAPET,          // crenellation merlon
  // Solid construction / geology / craft families (sealed unless noted). Species and dyes share
  // one nested world; parent palette colour is the flat-path fallback (VERSE_SUBVOXEL=0).
  MATERIAL_TEMPLATE_GRAVEL,           // loose particulate stone
  MATERIAL_TEMPLATE_BRICK,            // masonry courses with mortar
  MATERIAL_TEMPLATE_COBBLE,           // irregular paving stones
  MATERIAL_TEMPLATE_PLASTER,          // smooth stucco finish
  MATERIAL_TEMPLATE_PLANK,            // milled board grain (not bark)
  MATERIAL_TEMPLATE_STRAW,            // loose bedding fibres (not thatch shell)
  MATERIAL_TEMPLATE_WOOL,             // soft textile pile
  MATERIAL_TEMPLATE_CLOTH,            // woven fabric
  MATERIAL_TEMPLATE_SNOW,             // soft packed snow
  MATERIAL_TEMPLATE_ICE,              // crystalline ice
  MATERIAL_TEMPLATE_ADOBE,            // sun-dried mud brick
  MATERIAL_TEMPLATE_CLAY,             // dense clay body
  MATERIAL_TEMPLATE_TERRACOTTA,       // fired clay block (not roof shell)
  MATERIAL_TEMPLATE_CERAMIC,          // glazed pottery body
  MATERIAL_TEMPLATE_ASH,              // ash / charcoal dust
  MATERIAL_TEMPLATE_ORE,              // stone host with ore flecks
  MATERIAL_TEMPLATE_METAL,            // brushed / cast metal
  MATERIAL_TEMPLATE_CRYSTAL,          // faceted mineral crystal
  MATERIAL_TEMPLATE_BONE,             // porous bone
  MATERIAL_TEMPLATE_FLESH,            // soft tissue
  MATERIAL_TEMPLATE_FUNGUS,           // spongy fungal body (sparse gaps)
  MATERIAL_TEMPLATE_LEATHER,          // tanned hide grain
  MATERIAL_TEMPLATE_FUR,              // dense fur coat
  MATERIAL_TEMPLATE_FEATHER,          // fluffy plumage (sparse gaps)
  MATERIAL_TEMPLATE_SCALE,            // overlapping scales
  MATERIAL_TEMPLATE_SHELL,            // nacreous shell
  MATERIAL_TEMPLATE_HORN,             // keratin striations
  MATERIAL_TEMPLATE_PAPER,            // stacked parchment
  MATERIAL_TEMPLATE_ROPE,             // twisted fibres
  MATERIAL_TEMPLATE_WAX,              // soft wax
  MATERIAL_TEMPLATE_PLASTIC,          // smooth synthetic
  MATERIAL_TEMPLATE_RUBBER,           // matte elastomer
  MATERIAL_TEMPLATE_BEDROCK,          // dense non-destructible stone
  MATERIAL_TEMPLATE_OBSIDIAN,         // glassy volcanic rock
  MATERIAL_TEMPLATE_LIMESTONE_BLOCK,  // construction limestone block
  MATERIAL_TEMPLATE_CRAFTING_TABLE,  // workbench with pegboard
  MATERIAL_TEMPLATE_ANVIL,           // horned smithing anvil
  MATERIAL_TEMPLATE_FORGE,           // brick forge with firebox
  MATERIAL_TEMPLATE_COUNT
} MaterialTemplateKind;

// The six faces of a cube.
//
// The first three are the ones an isometric camera can see at once, and they keep their values
// because the atlas and the isometric renderer index by them. The other three exist for the
// first-person camera, which stands inside the world and can look at any side. Reusing the +Z bake
// for the -Z face would hang grass blades from the underside of an overhang; marching upward from
// the bottom of the template finds the soil that is actually there instead.
typedef enum
{
  MATERIAL_FACE_TOP = 0, // +Z, the face the player looks down on
  MATERIAL_FACE_LEFT,    // +Y
  MATERIAL_FACE_RIGHT,   // +X
  MATERIAL_FACE_BOTTOM,  // -Z
  MATERIAL_FACE_BACK,    // -Y, the side opposite LEFT
  MATERIAL_FACE_FRONT,   // -X, the side opposite RIGHT
  MATERIAL_FACE_COUNT
} MaterialFace;

// How many of the above an isometric camera can reach. The three visible faces come first so a
// caller that only draws those can stop here.
#define MATERIAL_FACE_ISO_COUNT 3

// One face baked to texels, in the ARGB8888 order SDL textures use.
//
// Colours here are albedo with only geometry-derived shading applied (sub-voxels sitting deeper
// below the surface come out darker, which reads as self-shadowing). Nothing light-dependent is
// baked in, because the renderer applies its own per-face shading and would otherwise apply it
// twice.
typedef struct
{
  uint32_t texels[MATERIAL_FACE_COUNT][MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE];
} MaterialFaceBake;

typedef struct
{
  MaterialTemplateKind kind;
  World *world; // MATERIAL_WORLD_SIZE^3 of sub-voxels; owned by the registry
  MaterialFaceBake bake;
  bool baked;
} MaterialTemplate;

// Build every template world and bake its faces. Idempotent; safe to call more than once.
// Costs one pass over MATERIAL_TEMPLATE_COUNT * 32^3 sub-voxels, done once at startup.
bool material_worlds_init(void);
void material_worlds_shutdown(void);

// The shared template for a kind, or NULL before init.
const MaterialTemplate *material_worlds_get(MaterialTemplateKind kind);

// The template that describes a parent voxel's surface, or NULL for a material with no template
// yet. Handles the type families: every VOXEL_WOOD_* maps to bark, every VOXEL_GRASS_* to grass.
const MaterialTemplate *material_worlds_for_voxel(VoxelType type);

// Generate one template world standalone. The caller owns the returned World. Exposed for tools
// and tests that want to inspect or export the sub-voxel data without standing up the registry.
World *material_world_generate(MaterialTemplateKind kind);

// Bake all six faces of a template world by finding, for each face texel, the first solid
// sub-voxel along the inward axis. Marches the full depth of the cube.
bool material_world_bake_faces(const World *world, MaterialFaceBake *out);

// As above, but stops after `max_depth` sub-voxels. This is what preserves gaps in a sparse
// material: a canopy marched to full depth almost always finds a leaf eventually and bakes solid,
// losing the sky that makes it read as foliage. material_worlds_init picks the depth per material.
bool material_world_bake_faces_depth(const World *world, int max_depth, MaterialFaceBake *out);

// The face whose outward normal points along `axis` (0 = x, 1 = y, 2 = z) in the given direction.
// Renderers that discover a surface by its normal — a raycast hit, a mesh quad — use this instead
// of hand-rolling the mapping, so all of them agree on which bake belongs to which side.
MaterialFace material_face_for_normal(int axis, bool positive);

// Sample a baked face on the CPU, for renderers that rasterise into a pixel buffer rather than
// handing UVs to the GPU.
//
// (u, v) run across the face. Only their fractional part is used, so a caller drawing a run of
// voxels can pass a coordinate in voxel units and get one tile per voxel for free. Returns false
// when the material has no template, or when the texel is transparent because the bake found
// nothing solid there — a gap in a canopy — leaving the outputs untouched so the caller can decide
// what to show through the hole.
bool material_worlds_sample(VoxelType type, MaterialFace face, float u, float v,
                            uint8_t *r, uint8_t *g, uint8_t *b);

// True when this voxel's template bakes with transparent gaps (angled roofs, panes, hollow props,
// foliage). Matches the seal_empty=false set in material_worlds_init.
bool material_worlds_is_sparse(VoxelType type);

// True when this template kind keeps air gaps (and should be drawn as nested geometry, not an AABB
// with punched face textures).
bool material_template_kind_is_sparse(MaterialTemplateKind kind);

const char *material_template_name(MaterialTemplateKind kind);
const char *material_face_name(MaterialFace face);

#endif // MATERIAL_WORLDS_H
