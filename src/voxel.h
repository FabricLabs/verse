#ifndef VOXEL_H
#define VOXEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declarations - World struct will be defined by including world.h

// Canonical VoxelType enumeration - single source of truth for all voxel types
typedef enum
{
  // Common base
  VOXEL_AIR = 0,
  VOXEL_BEDROCK, // non-destructible

  // Logical placeholder for dynamic entities rendered on the grid
  VOXEL_ACTOR,

  // Stones by hardness (hardest to softer)
  VOXEL_STONE, // generic stone
  VOXEL_STONE_BASALT,
  VOXEL_STONE_GRANITE,
  VOXEL_STONE_LIMESTONE,
  VOXEL_STONE_SANDSTONE,

  // Particulates
  VOXEL_GRAVEL, // particulate stone
  VOXEL_GRAVEL_BASALT,
  VOXEL_GRAVEL_GRANITE,
  VOXEL_GRAVEL_LIMESTONE,
  VOXEL_GRAVEL_SANDSTONE,
  VOXEL_SAND, // fine particulate stone
  VOXEL_SAND_BASALT,
  VOXEL_SAND_GRANITE,
  VOXEL_SAND_LIMESTONE,
  VOXEL_SAND_SANDSTONE,

  // Organics
  VOXEL_SOIL, // generic organic soil
  VOXEL_SOIL_CLAY, // clay soil
  VOXEL_SOIL_LOAM, // loam soil
  VOXEL_SOIL_SILT, // silt soil

  // Grass
  // Low turf coats: the parent voxel is a soil host (see MATERIAL_TEMPLATE_GRASS). Breaking one
  // scrapes the coat and leaves dirt rather than opening a hole.
  VOXEL_GRASS, // generic grass
  VOXEL_GRASS_WIDE, // nice wide-bladed grass
  VOXEL_GRASS_SHARP, // pointy sharp grass
  VOXEL_GRASS_CLOVER, // lush low clover
  VOXEL_GRASS_MOSS, // mossy grass

  // Bushes and Shrubs
  VOXEL_BUSH, // generic bush
  VOXEL_BUSH_FERN,
  VOXEL_BUSH_VINES,
  VOXEL_BUSH_THORNS,
  VOXEL_BUSH_BLUEBERRY,
  VOXEL_BUSH_BLACKBERRY,
  VOXEL_BUSH_RASPBERRY,
  VOXEL_BUSH_STRAWBERRY,

  // Trees
  VOXEL_WOOD, // generic wood
  VOXEL_WOOD_OAK,
  VOXEL_WOOD_BEECH,
  VOXEL_WOOD_BIRCH,
  VOXEL_WOOD_PINE,
  VOXEL_WOOD_PECAN,
  VOXEL_WOOD_LOCUST,
  VOXEL_WOOD_MAPLE,
  VOXEL_WOOD_ELM,
  VOXEL_WOOD_HAZELNUT,
  VOXEL_WOOD_CHESTNUT,
  VOXEL_WOOD_WILLOW,
  VOXEL_WOOD_WALNUT,
  VOXEL_WOOD_ACACIA,
  VOXEL_WOOD_COTTONWOOD,
  VOXEL_WOOD_CYPRESS,
  VOXEL_WOOD_SPRUCE,
  VOXEL_WOOD_JUNIPER,
  VOXEL_WOOD_REDWOOD,

  // Leaves
  VOXEL_LEAVES, // generic leaves
  VOXEL_LEAVES_OAK,
  VOXEL_LEAVES_BEECH,
  VOXEL_LEAVES_BIRCH,
  VOXEL_LEAVES_PINE,
  VOXEL_LEAVES_PECAN,
  VOXEL_LEAVES_LOCUST,
  VOXEL_LEAVES_MAPLE,
  VOXEL_LEAVES_ELM,
  VOXEL_LEAVES_HAZELNUT,
  VOXEL_LEAVES_CHESTNUT,
  VOXEL_LEAVES_WILLOW,
  VOXEL_LEAVES_WALNUT,
  VOXEL_LEAVES_ACACIA,
  VOXEL_LEAVES_COTTONWOOD,
  VOXEL_LEAVES_CYPRESS,
  VOXEL_LEAVES_SPRUCE,
  VOXEL_LEAVES_JUNIPER,
  VOXEL_LEAVES_REDWOOD,

  // Ores
  VOXEL_ORE, // generic ore
  VOXEL_ORE_COAL, // consumable coal
  VOXEL_ORE_ADAMANTITE, // rare ore
  VOXEL_ORE_HEMATITE, // rare ore
  VOXEL_ORE_MITHRIL, // rare ore
  VOXEL_ORE_COPPER,
  VOXEL_ORE_SILVER,
  VOXEL_ORE_GOLD,
  VOXEL_ORE_TIN,
  VOXEL_ORE_IRON,
  VOXEL_ORE_LEAD,
  VOXEL_ORE_ZINC,
  VOXEL_ORE_TITANIUM,
  VOXEL_ORE_ALUMINUM,
  VOXEL_ORE_MAGNESIUM,
  VOXEL_ORE_COBALT,
  VOXEL_ORE_NICKEL,
  VOXEL_ORE_PLATINUM,

  // Refined metals (results of smelting ores)
  VOXEL_ADAMANTITE,
  VOXEL_HEMATITE,
  VOXEL_MITHRIL,
  VOXEL_COPPER,
  VOXEL_SILVER,
  VOXEL_GOLD,
  VOXEL_TIN,
  VOXEL_IRON,
  VOXEL_LEAD,
  VOXEL_ZINC,
  VOXEL_STEEL,
  VOXEL_TITANIUM,
  VOXEL_ALUMINUM,
  VOXEL_MAGNESIUM,
  VOXEL_COBALT,
  VOXEL_NICKEL,
  VOXEL_PLATINUM,

  // Fluids and vapors
  VOXEL_WATER,
  VOXEL_MAGMA,
  VOXEL_STEAM,
  VOXEL_OIL,
  VOXEL_GAS,

  // Springs (special behavior)
  VOXEL_SPRING,
  VOXEL_SPRING_WATER,
  VOXEL_SPRING_MAGMA,
  VOXEL_SPRING_STEAM,
  VOXEL_SPRING_OIL,
  VOXEL_SPRING_GAS,

  // Rare/nice things
  VOXEL_CRYSTAL,
  VOXEL_CRYSTAL_RED,
  VOXEL_CRYSTAL_GREEN,
  VOXEL_CRYSTAL_BLUE,

  // Life
  VOXEL_BONE,
  VOXEL_FLESH,
  VOXEL_ORGAN,
  VOXEL_BLOOD,
  VOXEL_BRAIN,

  // Fungus
  VOXEL_FUNGUS,

  // Other material types
  VOXEL_GLASS,
  VOXEL_BRICK,
  VOXEL_LIMESTONE,
  VOXEL_OBSIDIAN,
  VOXEL_CLAY,      // solid clay block (not soil)
  VOXEL_WOOL,      // generic wool (see VOXEL_WOOL_* dyes)
  VOXEL_SNOW,      // snow block
  VOXEL_ICE,
  VOXEL_PLASTIC,
  VOXEL_CLOTH,

  // Tall grass: a decorative tuft that stands on top of a grass surface and does not block
  // movement, as distinct from VOXEL_GRASS and friends, which are the ground itself.
  //
  // It is down here rather than up with the other grasses on purpose. Worlds are serialised with
  // the numeric value of this enum, and g_voxel_type_mass_kg is a positional array, so inserting a
  // type in the middle would renumber everything after it: every saved world would come back with
  // its materials shifted by one, and every mass would belong to the wrong material. Appending is
  // the only edit that leaves existing values alone.
  VOXEL_GRASS_TALL,

  // Lit props. Appended for save compatibility (same rule as VOXEL_GRASS_TALL above).
  // The voxel is the fixture; flame is a particle effect emitted from its top face.
  VOXEL_CANDLE,
  VOXEL_CAMPFIRE,

  // Gameplay composition materials (appended for save compatibility).
  // Settlements, dungeon props, fauna models, and furniture need these beyond raw geology.
  VOXEL_PLANK, // milled boards for floors, furniture, shutters
  VOXEL_THATCH, // roof thatch
  VOXEL_STRAW, // loose straw / bedding
  VOXEL_COBBLE, // paved cobblestone
  VOXEL_PLASTER, // interior plaster / stucco
  VOXEL_TERRACOTTA, // fired clay roof tile / pottery body
  VOXEL_ADOBE, // sun-dried mud brick
  VOXEL_GLASS_WHITE, // frosted / milk glass
  VOXEL_GLASS_RED, // stained glass
  VOXEL_GLASS_GREEN, // stained glass
  VOXEL_GLASS_BLUE, // stained glass
  VOXEL_GLASS_YELLOW, // stained glass
  VOXEL_WOOL_WHITE, // dyed wool
  VOXEL_WOOL_BLACK, // dyed wool
  VOXEL_WOOL_BROWN, // dyed wool
  VOXEL_WOOL_GRAY, // dyed wool
  VOXEL_WOOL_RED, // dyed wool
  VOXEL_WOOL_BLUE, // dyed wool
  VOXEL_WOOL_GREEN, // dyed wool
  VOXEL_WOOL_YELLOW, // dyed wool
  VOXEL_LEATHER, // tanned hide
  VOXEL_FUR, // creature fur (distinct from wool)
  VOXEL_FEATHER, // bird plumage
  VOXEL_SCALE, // reptile / fish scale
  VOXEL_SHELL, // shell / carapace
  VOXEL_HORN, // keratin: horn, beak, claw
  VOXEL_PAPER, // paper, parchment, books
  VOXEL_ROPE, // rope / twine
  VOXEL_CERAMIC, // glazed pottery
  VOXEL_RUBBER, // tires, seals, soft synthetic
  VOXEL_WAX, // candle wax / seals
  VOXEL_ASH, // ash / charcoal dust

  // Building fittings (appended for save compatibility).
  VOXEL_DOOR, // wooden door panel — passable so interiors stay enterable
  VOXEL_ROOF_TILE, // overlapping fired-clay shingles for pitched roofs
  VOXEL_CRATE, // wooden storage crate (occupation props)
  VOXEL_BARREL, // storage barrel
  VOXEL_BED, // straw/wool bed
  // Oriented / mirrored fittings (append-only). Pane normals match the wall they sit in.
  VOXEL_DOOR_NS,           // door panel in the Y mid-plane (N/S wall openings)
  VOXEL_THATCH_MIRROR,     // thatch shell sloping the opposite way (right roof half)
  VOXEL_ROOF_TILE_MIRROR,  // clay tile shell, mirrored slope
  VOXEL_GLASS_NS,          // glass pane in the Y mid-plane (N/S wall windows)

  // Furniture / stair fittings (append-only). Sparse 32³ material templates.
  VOXEL_STAIR,             // stepped wedge rising along +X
  VOXEL_STAIR_NS,          // stepped wedge rising along +Y
  VOXEL_CHAIR,             // seat + backrest
  VOXEL_TABLE,             // four legs + top slab
  VOXEL_CHEST,             // closed storage chest (inset solid box)

  // Perimeter fences / fortifications (append-only).
  VOXEL_FENCE,             // wooden post-and-rail (rails run along Y — E/W runs)
  VOXEL_FENCE_NS,          // wooden post-and-rail (rails run along X — N/S runs)
  VOXEL_FENCE_WATTLE,      // woven brush / hurdle fence
  VOXEL_FENCE_IRON,        // iron palings / bars
  VOXEL_RAMPART,           // stone walkway slab for wall tops
  VOXEL_PARAPET,           // crenellation merlon on a rampart

  // Crafting stations (append-only).
  VOXEL_CRAFTING_TABLE,    // woodworking bench
  VOXEL_ANVIL,             // smithing anvil
  VOXEL_FORGE,             // brick forge / furnace

  // Final Values
  VOXEL_COUNT,
  VOXEL_WORLD = 255 // World reference
} VoxelType;

// ============================================================================
// VOXEL COLOR CONSTANTS
// ============================================================================

// RGB color constants for each voxel type (0xRRGGBB format)
// Stone colors are ordered by hardness: basalt (hardest/darkest) to sandstone (softest/lightest)
#define VOXEL_COLOR_AIR           0x000000
#define VOXEL_COLOR_BEDROCK       0x323232
#define VOXEL_COLOR_ACTOR         0xFFC850

// Stone colors by hardness (darkest to lightest)
#define VOXEL_COLOR_STONE         0x808080
#define VOXEL_COLOR_STONE_BASALT  0x3C3C41  // Darkest - hardest igneous rock
#define VOXEL_COLOR_STONE_GRANITE 0x786E6E  // Medium-dark - hard igneous rock
#define VOXEL_COLOR_STONE_LIMESTONE 0xB4B4A0 // Medium-light - medium sedimentary rock
#define VOXEL_COLOR_STONE_SANDSTONE 0xD2B48C // Lightest - softest sedimentary rock

// Gravel colors (particulate stone variants)
#define VOXEL_COLOR_GRAVEL        0x8C8278
#define VOXEL_COLOR_GRAVEL_BASALT 0x46464B
#define VOXEL_COLOR_GRAVEL_GRANITE 0x827878
#define VOXEL_COLOR_GRAVEL_LIMESTONE 0xBEBEAA
#define VOXEL_COLOR_GRAVEL_SANDSTONE 0xC8B4A0

// Sand colors (fine particulate stone variants)
#define VOXEL_COLOR_SAND          0xF4A460
#define VOXEL_COLOR_SAND_BASALT   0x505055
#define VOXEL_COLOR_SAND_GRANITE  0x8C8282
#define VOXEL_COLOR_SAND_LIMESTONE 0xC8C8B4
#define VOXEL_COLOR_SAND_SANDSTONE 0xD2B48C

// Soil colors
#define VOXEL_COLOR_SOIL          0x643C1E
#define VOXEL_COLOR_SOIL_CLAY     0x96503C
#define VOXEL_COLOR_SOIL_LOAM     0x785A3C
#define VOXEL_COLOR_SOIL_SILT     0x6E6450

// Grass colors
#define VOXEL_COLOR_GRASS         0x5AAA32
#define VOXEL_COLOR_GRASS_WIDE    0x64B93C
#define VOXEL_COLOR_GRASS_SHARP   0x55A530
#define VOXEL_COLOR_GRASS_CLOVER  0x469628
#define VOXEL_COLOR_GRASS_MOSS    0x3C8C23
#define VOXEL_COLOR_GRASS_TALL    0x6FBF3F

// Lit props: warm wax / charred wood. The flame itself is particles, not this colour.
#define VOXEL_COLOR_CANDLE        0xE8D9A8
#define VOXEL_COLOR_CAMPFIRE      0x3A2A18
#define VOXEL_COLOR_PLASTIC        0xE8E8F0
#define VOXEL_COLOR_CLOTH          0xA08060
#define VOXEL_COLOR_PLANK          0xC4A574
#define VOXEL_COLOR_THATCH         0xC8A85A
#define VOXEL_COLOR_STRAW          0xD2B86A
#define VOXEL_COLOR_COBBLE         0x7A7A72
#define VOXEL_COLOR_PLASTER        0xE8E0D0
#define VOXEL_COLOR_TERRACOTTA     0xC45A2A
#define VOXEL_COLOR_ADOBE          0xB8895A
#define VOXEL_COLOR_GLASS_WHITE    0xF0F4F8
#define VOXEL_COLOR_GLASS_RED      0xC83030
#define VOXEL_COLOR_GLASS_GREEN    0x30A050
#define VOXEL_COLOR_GLASS_BLUE     0x3060C8
#define VOXEL_COLOR_GLASS_YELLOW   0xE0C030
#define VOXEL_COLOR_WOOL_WHITE     0xF5F5F5
#define VOXEL_COLOR_WOOL_BLACK     0x2A2A2A
#define VOXEL_COLOR_WOOL_BROWN     0x8B5A2B
#define VOXEL_COLOR_WOOL_GRAY      0x9A9A9A
#define VOXEL_COLOR_WOOL_RED       0xB03030
#define VOXEL_COLOR_WOOL_BLUE      0x3050A0
#define VOXEL_COLOR_WOOL_GREEN     0x3A8A40
#define VOXEL_COLOR_WOOL_YELLOW    0xE0C040
#define VOXEL_COLOR_LEATHER        0x8B5A2B
#define VOXEL_COLOR_FUR            0x6B4A2A
#define VOXEL_COLOR_FEATHER        0xD8D0C0
#define VOXEL_COLOR_SCALE          0x4A7A50
#define VOXEL_COLOR_SHELL          0xE8D8C0
#define VOXEL_COLOR_HORN           0xC8A060
#define VOXEL_COLOR_PAPER          0xF0E8D0
#define VOXEL_COLOR_ROPE           0xA89050
#define VOXEL_COLOR_CERAMIC        0xD0C8B8
#define VOXEL_COLOR_RUBBER         0x2C2C2C
#define VOXEL_COLOR_WAX            0xE8D9A0
#define VOXEL_COLOR_ASH            0x6E6E6E
#define VOXEL_COLOR_DOOR           0x6B4423
#define VOXEL_COLOR_ROOF_TILE      0xA84828
#define VOXEL_COLOR_CRATE          0xA07840
#define VOXEL_COLOR_BARREL         0x6B4A28
#define VOXEL_COLOR_BED            0x8B5A3C
#define VOXEL_COLOR_DOOR_NS        VOXEL_COLOR_DOOR
#define VOXEL_COLOR_THATCH_MIRROR  0xC8A050
#define VOXEL_COLOR_ROOF_TILE_MIRROR VOXEL_COLOR_ROOF_TILE
#define VOXEL_COLOR_GLASS_NS       0xA8D8E8
#define VOXEL_COLOR_STAIR          0x9A7040
#define VOXEL_COLOR_STAIR_NS       VOXEL_COLOR_STAIR
#define VOXEL_COLOR_CHAIR          0x8B5A2B
#define VOXEL_COLOR_TABLE          0xA07848
#define VOXEL_COLOR_CHEST          0x6B4423
#define VOXEL_COLOR_FENCE          0x8B6914
#define VOXEL_COLOR_FENCE_NS       VOXEL_COLOR_FENCE
#define VOXEL_COLOR_FENCE_WATTLE   0x9A7B4F
#define VOXEL_COLOR_FENCE_IRON     0x4A4A50
#define VOXEL_COLOR_RAMPART        0x6E6E72
#define VOXEL_COLOR_PARAPET        0x5A5A5E
#define VOXEL_COLOR_CRAFTING_TABLE 0xA07840
#define VOXEL_COLOR_ANVIL          0x6E6E74
#define VOXEL_COLOR_FORGE          0x8A4A32

// Bush colors
#define VOXEL_COLOR_BUSH          0x32781E
#define VOXEL_COLOR_BUSH_FERN     0x286E19
#define VOXEL_COLOR_BUSH_VINES    0x236414
#define VOXEL_COLOR_BUSH_THORNS   0x3C8223
#define VOXEL_COLOR_BUSH_BLUEBERRY 0x2D731C
#define VOXEL_COLOR_BUSH_BLACKBERRY 0x377D20
#define VOXEL_COLOR_BUSH_RASPBERRY 0x32781E
#define VOXEL_COLOR_BUSH_STRAWBERRY 0x30761D

// Wood colors
#define VOXEL_COLOR_WOOD          0x8B5A2B
#define VOXEL_COLOR_WOOD_OAK      0x8B5A2B
#define VOXEL_COLOR_WOOD_BEECH    0xA07850
#define VOXEL_COLOR_WOOD_BIRCH    0xCDBE96
#define VOXEL_COLOR_WOOD_PINE     0x6E4B2D
#define VOXEL_COLOR_WOOD_PECAN    0x915F32
#define VOXEL_COLOR_WOOD_LOCUST   0x966437
#define VOXEL_COLOR_WOOD_MAPLE    0x9B693C
#define VOXEL_COLOR_WOOD_ELM      0x875528
#define VOXEL_COLOR_WOOD_HAZELNUT 0x8C5A2D
#define VOXEL_COLOR_WOOD_CHESTNUT 0x915F32
#define VOXEL_COLOR_WOOD_WILLOW   0x784623
#define VOXEL_COLOR_WOOD_WALNUT   0x966437
#define VOXEL_COLOR_WOOD_ACACIA   0xA06E41
#define VOXEL_COLOR_WOOD_COTTONWOOD 0x7D4B28
#define VOXEL_COLOR_WOOD_CYPRESS  0x734628
#define VOXEL_COLOR_WOOD_SPRUCE   0x694123
#define VOXEL_COLOR_WOOD_JUNIPER  0x643C1E
#define VOXEL_COLOR_WOOD_REDWOOD  0x5F3719

// Leaves colors
#define VOXEL_COLOR_LEAVES        0x228B22
#define VOXEL_COLOR_LEAVES_OAK    0x228B22
#define VOXEL_COLOR_LEAVES_BEECH  0x289628
#define VOXEL_COLOR_LEAVES_BIRCH  0xB4C8A0
#define VOXEL_COLOR_LEAVES_PINE   0x32783C
#define VOXEL_COLOR_LEAVES_PECAN  0x249124
#define VOXEL_COLOR_LEAVES_LOCUST 0x269326
#define VOXEL_COLOR_LEAVES_MAPLE  0x2A9B2A
#define VOXEL_COLOR_LEAVES_ELM    0x208720
#define VOXEL_COLOR_LEAVES_HAZELNUT 0x238C23
#define VOXEL_COLOR_LEAVES_CHESTNUT 0x249124
#define VOXEL_COLOR_LEAVES_WILLOW 0x1E821E
#define VOXEL_COLOR_LEAVES_WALNUT 0x269326
#define VOXEL_COLOR_LEAVES_ACACIA 0x2CA02C
#define VOXEL_COLOR_LEAVES_COTTONWOOD 0x218921
#define VOXEL_COLOR_LEAVES_CYPRESS 0x1F841F
#define VOXEL_COLOR_LEAVES_SPRUCE 0x1D801D
#define VOXEL_COLOR_LEAVES_JUNIPER 0x1C7D1C
#define VOXEL_COLOR_LEAVES_REDWOOD 0x1B7A1B

// Ore colors
#define VOXEL_COLOR_ORE           0x646464
#define VOXEL_COLOR_ORE_COAL      0x1E1E1E
#define VOXEL_COLOR_ORE_ADAMANTITE 0x505078
#define VOXEL_COLOR_ORE_HEMATITE  0x783C3C
#define VOXEL_COLOR_ORE_MITHRIL   0x8C8CB4
#define VOXEL_COLOR_ORE_COPPER    0xB87333
#define VOXEL_COLOR_ORE_SILVER    0xB4B4C8
#define VOXEL_COLOR_ORE_GOLD      0xD4AF37
#define VOXEL_COLOR_ORE_TIN       0xC8C8C8
#define VOXEL_COLOR_ORE_IRON      0xAA8C78
#define VOXEL_COLOR_ORE_LEAD      0x646464
#define VOXEL_COLOR_ORE_ZINC      0xB4B4B4
#define VOXEL_COLOR_ORE_TITANIUM  0xA0A0A0
#define VOXEL_COLOR_ORE_ALUMINUM  0xDCDCDC
#define VOXEL_COLOR_ORE_MAGNESIUM 0xC8C8C8
#define VOXEL_COLOR_ORE_COBALT    0x3C3C78
#define VOXEL_COLOR_ORE_NICKEL    0x787878
#define VOXEL_COLOR_ORE_PLATINUM  0xC8C8DC

// Refined metal colors
#define VOXEL_COLOR_ADAMANTITE    0x64648C
#define VOXEL_COLOR_HEMATITE      0x8C4646
#define VOXEL_COLOR_MITHRIL       0xA0A0C8
#define VOXEL_COLOR_COPPER        0xC67D3A
#define VOXEL_COLOR_SILVER        0xC8C8DC
#define VOXEL_COLOR_GOLD          0xFFD700
#define VOXEL_COLOR_TIN           0xDCDCDC
#define VOXEL_COLOR_IRON          0xBEBEC8
#define VOXEL_COLOR_LEAD          0x787878
#define VOXEL_COLOR_ZINC          0xC8C8C8
#define VOXEL_COLOR_STEEL         0xB4B4BE
#define VOXEL_COLOR_TITANIUM      0xB4B4B4
#define VOXEL_COLOR_ALUMINUM      0xF0F0F0
#define VOXEL_COLOR_MAGNESIUM     0xDCDCDC
#define VOXEL_COLOR_COBALT        0x50508C
#define VOXEL_COLOR_NICKEL        0x8C8C8C
#define VOXEL_COLOR_PLATINUM      0xDCDCE0

// Fluid and vapor colors
#define VOXEL_COLOR_WATER         0x1E90FF
#define VOXEL_COLOR_MAGMA         0xFF5A14
#define VOXEL_COLOR_STEAM         0xF5F5EB
#define VOXEL_COLOR_OIL           0x323232
#define VOXEL_COLOR_GAS           0xC8C8C8

// Spring colors
#define VOXEL_COLOR_SPRING        0x64C8FF
#define VOXEL_COLOR_SPRING_WATER  0x1E90FF
#define VOXEL_COLOR_SPRING_MAGMA  0xFF5A14
#define VOXEL_COLOR_SPRING_STEAM  0xF5F5EB
#define VOXEL_COLOR_SPRING_OIL    0x323232
#define VOXEL_COLOR_SPRING_GAS    0xC8C8C8

// Crystal colors
#define VOXEL_COLOR_CRYSTAL       0x64C8FF
#define VOXEL_COLOR_CRYSTAL_RED   0xFF7878
#define VOXEL_COLOR_CRYSTAL_GREEN 0x64FFB4
#define VOXEL_COLOR_CRYSTAL_BLUE  0x64C8FF

// Life colors
#define VOXEL_COLOR_BONE          0xF0F0DC
#define VOXEL_COLOR_FLESH         0xFFC8C8
#define VOXEL_COLOR_ORGAN         0xC86464
#define VOXEL_COLOR_BLOOD         0x960000
#define VOXEL_COLOR_BRAIN         0xB47878

// Other material colors
#define VOXEL_COLOR_FUNGUS        0x64503C
#define VOXEL_COLOR_GLASS         0xC8DCF0
#define VOXEL_COLOR_BRICK         0xB22222
#define VOXEL_COLOR_LIMESTONE     0x808080
#define VOXEL_COLOR_OBSIDIAN      0x14121E
#define VOXEL_COLOR_CLAY          0xA0AAB4
#define VOXEL_COLOR_WOOL          0xDCDCDC
#define VOXEL_COLOR_SNOW          0xFAFAFA
#define VOXEL_COLOR_ICE           0xA0C8FF
#define VOXEL_COLOR_WORLD         0xFF00FF

// ============================================================================
// VOXEL PHYSICAL PROPERTIES
// ============================================================================

// Physical scale derived from models' humanRef (32px tile, 30px human ≈ 1.8 m)
// metersPerVoxel = 1.8 / 30 = 0.06 m
#define VOXEL_METERS_PER_SIDE 0.06f
#define VOXEL_VOLUME_M3 (VOXEL_METERS_PER_SIDE * VOXEL_METERS_PER_SIDE * VOXEL_METERS_PER_SIDE)

// Per-type mass (kg) for a single voxel at the canonical scale above.
// This is a fixed lookup (no allocations) based on typical material densities.
// Densities (kg/m^3) approximations: air~1.2, water~1000, wood~600, leaves~200,
// sand~1600, bedrock/basalt~3000, granite~2700, limestone~2600, stone~2600,
// soils~1300-1500, low-grass (soil host)~1400, tall grass~120, ores 4500-10500,
// metals 8000-19300, crystal~2500,
// magma ~ 2650 (lava similar to basalt, slightly less).
// mass = density * volume.
static const float g_voxel_type_mass_kg[VOXEL_COUNT] = {
    /* VOXEL_AIR */ 1.2f * VOXEL_VOLUME_M3,
    /* VOXEL_BEDROCK */ 3000.f * VOXEL_VOLUME_M3,
    /* VOXEL_ACTOR */ 985.f * VOXEL_VOLUME_M3, // approx human average density
    /* VOXEL_STONE */ 2600.f * VOXEL_VOLUME_M3,
    /* VOXEL_STONE_BASALT */ 3000.f * VOXEL_VOLUME_M3,
    /* VOXEL_STONE_GRANITE */ 2700.f * VOXEL_VOLUME_M3,
    /* VOXEL_STONE_LIMESTONE */ 2600.f * VOXEL_VOLUME_M3,
    /* VOXEL_STONE_SANDSTONE */ 2200.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRAVEL */ 1600.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRAVEL_BASALT */ 1800.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRAVEL_GRANITE */ 1700.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRAVEL_LIMESTONE */ 1600.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRAVEL_SANDSTONE */ 1400.f * VOXEL_VOLUME_M3,
    /* VOXEL_SAND */ 1600.f * VOXEL_VOLUME_M3,
    /* VOXEL_SAND_BASALT */ 1800.f * VOXEL_VOLUME_M3,
    /* VOXEL_SAND_GRANITE */ 1700.f * VOXEL_VOLUME_M3,
    /* VOXEL_SAND_LIMESTONE */ 1600.f * VOXEL_VOLUME_M3,
    /* VOXEL_SAND_SANDSTONE */ 1400.f * VOXEL_VOLUME_M3,
    /* VOXEL_SOIL */ 1400.f * VOXEL_VOLUME_M3,
    /* VOXEL_SOIL_CLAY */ 1500.f * VOXEL_VOLUME_M3,
    /* VOXEL_SOIL_LOAM */ 1400.f * VOXEL_VOLUME_M3,
    /* VOXEL_SOIL_SILT */ 1350.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRASS */ 1400.f * VOXEL_VOLUME_M3, // soil host + thin turf coat
    /* VOXEL_GRASS_WIDE */ 1400.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRASS_SHARP */ 1400.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRASS_CLOVER */ 1400.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRASS_MOSS */ 1400.f * VOXEL_VOLUME_M3,
    /* VOXEL_BUSH */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_BUSH_FERN */ 350.f * VOXEL_VOLUME_M3,
    /* VOXEL_BUSH_VINES */ 300.f * VOXEL_VOLUME_M3,
    /* VOXEL_BUSH_THORNS */ 450.f * VOXEL_VOLUME_M3,
    /* VOXEL_BUSH_BLUEBERRY */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_BUSH_BLACKBERRY */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_BUSH_RASPBERRY */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_BUSH_STRAWBERRY */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD */ 600.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_OAK */ 700.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_BEECH */ 650.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_BIRCH */ 650.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_PINE */ 550.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_PECAN */ 700.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_LOCUST */ 750.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_MAPLE */ 650.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_ELM */ 600.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_HAZELNUT */ 650.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_CHESTNUT */ 700.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_WILLOW */ 500.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_WALNUT */ 700.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_ACACIA */ 800.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_COTTONWOOD */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_CYPRESS */ 550.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_SPRUCE */ 500.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_JUNIPER */ 600.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOD_REDWOOD */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_OAK */ 220.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_BEECH */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_BIRCH */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_PINE */ 180.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_PECAN */ 220.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_LOCUST */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_MAPLE */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_ELM */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_HAZELNUT */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_CHESTNUT */ 220.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_WILLOW */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_WALNUT */ 220.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_ACACIA */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_COTTONWOOD */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_CYPRESS */ 180.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_SPRUCE */ 180.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_JUNIPER */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAVES_REDWOOD */ 180.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE */ 6000.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_COAL */ 1350.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_ADAMANTITE */ 8000.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_HEMATITE */ 7500.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_MITHRIL */ 8500.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_COPPER */ 6000.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_SILVER */ 7500.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_GOLD */ 10000.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_TIN */ 5500.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_IRON */ 7870.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_LEAD */ 11340.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_ZINC */ 7140.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_TITANIUM */ 4500.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_ALUMINUM */ 2700.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_MAGNESIUM */ 1740.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_COBALT */ 8900.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_NICKEL */ 8900.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORE_PLATINUM */ 21450.f * VOXEL_VOLUME_M3,
    /* VOXEL_ADAMANTITE */ 8000.f * VOXEL_VOLUME_M3,
    /* VOXEL_HEMATITE */ 7500.f * VOXEL_VOLUME_M3,
    /* VOXEL_MITHRIL */ 8500.f * VOXEL_VOLUME_M3,
    /* VOXEL_COPPER */ 8960.f * VOXEL_VOLUME_M3,
    /* VOXEL_SILVER */ 10490.f * VOXEL_VOLUME_M3,
    /* VOXEL_GOLD */ 19300.f * VOXEL_VOLUME_M3,
    /* VOXEL_TIN */ 7280.f * VOXEL_VOLUME_M3,
    /* VOXEL_IRON */ 7870.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEAD */ 11340.f * VOXEL_VOLUME_M3,
    /* VOXEL_ZINC */ 7140.f * VOXEL_VOLUME_M3,
    /* VOXEL_STEEL */ 7870.f * VOXEL_VOLUME_M3,
    /* VOXEL_TITANIUM */ 4500.f * VOXEL_VOLUME_M3,
    /* VOXEL_ALUMINUM */ 2700.f * VOXEL_VOLUME_M3,
    /* VOXEL_MAGNESIUM */ 1740.f * VOXEL_VOLUME_M3,
    /* VOXEL_COBALT */ 8900.f * VOXEL_VOLUME_M3,
    /* VOXEL_NICKEL */ 8900.f * VOXEL_VOLUME_M3,
    /* VOXEL_PLATINUM */ 21450.f * VOXEL_VOLUME_M3,
    /* VOXEL_WATER */ 1000.f * VOXEL_VOLUME_M3,
    /* VOXEL_MAGMA */ 2650.f * VOXEL_VOLUME_M3,
    /* VOXEL_STEAM */ 0.6f * VOXEL_VOLUME_M3,
    /* VOXEL_OIL */ 800.f * VOXEL_VOLUME_M3,
    /* VOXEL_GAS */ 0.7f * VOXEL_VOLUME_M3,
    /* VOXEL_SPRING */ 1000.f * VOXEL_VOLUME_M3,
    /* VOXEL_SPRING_WATER */ 1000.f * VOXEL_VOLUME_M3,
    /* VOXEL_SPRING_MAGMA */ 2650.f * VOXEL_VOLUME_M3,
    /* VOXEL_SPRING_STEAM */ 0.6f * VOXEL_VOLUME_M3,
    /* VOXEL_SPRING_OIL */ 800.f * VOXEL_VOLUME_M3,
    /* VOXEL_SPRING_GAS */ 0.7f * VOXEL_VOLUME_M3,
    /* VOXEL_CRYSTAL */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_CRYSTAL_RED */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_CRYSTAL_GREEN */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_CRYSTAL_BLUE */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_BONE */ 1900.f * VOXEL_VOLUME_M3,
    /* VOXEL_FLESH */ 1000.f * VOXEL_VOLUME_M3,
    /* VOXEL_ORGAN */ 1050.f * VOXEL_VOLUME_M3,
    /* VOXEL_BLOOD */ 1060.f * VOXEL_VOLUME_M3,
    /* VOXEL_BRAIN */ 1030.f * VOXEL_VOLUME_M3,
    /* VOXEL_FUNGUS */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_GLASS */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_BRICK */ 1920.f * VOXEL_VOLUME_M3,
    /* VOXEL_LIMESTONE */ 2600.f * VOXEL_VOLUME_M3,
    /* VOXEL_OBSIDIAN */ 2600.f * VOXEL_VOLUME_M3,
    /* VOXEL_CLAY */ 1800.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOL */ 100.f * VOXEL_VOLUME_M3,
    /* VOXEL_SNOW */ 300.f * VOXEL_VOLUME_M3,
    /* VOXEL_ICE */ 917.f * VOXEL_VOLUME_M3,
    // These two were missing, and a short initialiser list is not an error in C: they were reading
    // as a density of zero, which makes a plastic block weightless and lighter than air.
    /* VOXEL_PLASTIC */ 950.f * VOXEL_VOLUME_M3,
    /* VOXEL_CLOTH */ 300.f * VOXEL_VOLUME_M3,
    /* VOXEL_GRASS_TALL */ 120.f * VOXEL_VOLUME_M3, // mostly air between the blades
    /* VOXEL_CANDLE */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_CAMPFIRE */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_PLANK */ 700.f * VOXEL_VOLUME_M3,
    /* VOXEL_THATCH */ 80.f * VOXEL_VOLUME_M3,
    /* VOXEL_STRAW */ 50.f * VOXEL_VOLUME_M3,
    /* VOXEL_COBBLE */ 2400.f * VOXEL_VOLUME_M3,
    /* VOXEL_PLASTER */ 1200.f * VOXEL_VOLUME_M3,
    /* VOXEL_TERRACOTTA */ 1800.f * VOXEL_VOLUME_M3,
    /* VOXEL_ADOBE */ 1600.f * VOXEL_VOLUME_M3,
    /* VOXEL_GLASS_WHITE */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_GLASS_RED */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_GLASS_GREEN */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_GLASS_BLUE */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_GLASS_YELLOW */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOL_WHITE */ 100.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOL_BLACK */ 100.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOL_BROWN */ 100.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOL_GRAY */ 100.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOL_RED */ 100.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOL_BLUE */ 100.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOL_GREEN */ 100.f * VOXEL_VOLUME_M3,
    /* VOXEL_WOOL_YELLOW */ 100.f * VOXEL_VOLUME_M3,
    /* VOXEL_LEATHER */ 860.f * VOXEL_VOLUME_M3,
    /* VOXEL_FUR */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_FEATHER */ 40.f * VOXEL_VOLUME_M3,
    /* VOXEL_SCALE */ 1200.f * VOXEL_VOLUME_M3,
    /* VOXEL_SHELL */ 1600.f * VOXEL_VOLUME_M3,
    /* VOXEL_HORN */ 1300.f * VOXEL_VOLUME_M3,
    /* VOXEL_PAPER */ 800.f * VOXEL_VOLUME_M3,
    /* VOXEL_ROPE */ 500.f * VOXEL_VOLUME_M3,
    /* VOXEL_CERAMIC */ 2200.f * VOXEL_VOLUME_M3,
    /* VOXEL_RUBBER */ 1100.f * VOXEL_VOLUME_M3,
    /* VOXEL_WAX */ 900.f * VOXEL_VOLUME_M3,
    /* VOXEL_ASH */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_DOOR */ 650.f * VOXEL_VOLUME_M3,
    /* VOXEL_ROOF_TILE */ 1800.f * VOXEL_VOLUME_M3,
    /* VOXEL_CRATE */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_BARREL */ 350.f * VOXEL_VOLUME_M3,
    /* VOXEL_BED */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_DOOR_NS */ 650.f * VOXEL_VOLUME_M3,
    /* VOXEL_THATCH_MIRROR */ 80.f * VOXEL_VOLUME_M3,
    /* VOXEL_ROOF_TILE_MIRROR */ 1800.f * VOXEL_VOLUME_M3,
    /* VOXEL_GLASS_NS */ 2500.f * VOXEL_VOLUME_M3,
    /* VOXEL_STAIR */ 650.f * VOXEL_VOLUME_M3,
    /* VOXEL_STAIR_NS */ 650.f * VOXEL_VOLUME_M3,
    /* VOXEL_CHAIR */ 300.f * VOXEL_VOLUME_M3,
    /* VOXEL_TABLE */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_CHEST */ 450.f * VOXEL_VOLUME_M3,
    /* VOXEL_FENCE */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_FENCE_NS */ 200.f * VOXEL_VOLUME_M3,
    /* VOXEL_FENCE_WATTLE */ 120.f * VOXEL_VOLUME_M3,
    /* VOXEL_FENCE_IRON */ 400.f * VOXEL_VOLUME_M3,
    /* VOXEL_RAMPART */ 1800.f * VOXEL_VOLUME_M3,
    /* VOXEL_PARAPET */ 1600.f * VOXEL_VOLUME_M3,

    /* VOXEL_CRAFTING_TABLE */ 450.f * VOXEL_VOLUME_M3,
    /* VOXEL_ANVIL */ 7800.f * VOXEL_VOLUME_M3,
    /* VOXEL_FORGE */ 1800.f * VOXEL_VOLUME_M3
};

// ============================================================================
// VOXEL OPACITY CONSTANTS
// ============================================================================

// Opacity values (0.0 = transparent, 1.0 = opaque) for each voxel type
#define VOXEL_OPACITY_AIR           0.0f
#define VOXEL_OPACITY_BEDROCK       1.0f
#define VOXEL_OPACITY_ACTOR         1.0f
#define VOXEL_OPACITY_STONE         1.0f
#define VOXEL_OPACITY_STONE_BASALT  1.0f
#define VOXEL_OPACITY_STONE_GRANITE 1.0f
#define VOXEL_OPACITY_STONE_LIMESTONE 1.0f
#define VOXEL_OPACITY_STONE_SANDSTONE 1.0f
#define VOXEL_OPACITY_GRAVEL        1.0f
#define VOXEL_OPACITY_GRAVEL_BASALT 1.0f
#define VOXEL_OPACITY_GRAVEL_GRANITE 1.0f
#define VOXEL_OPACITY_GRAVEL_LIMESTONE 1.0f
#define VOXEL_OPACITY_GRAVEL_SANDSTONE 1.0f
#define VOXEL_OPACITY_SAND          1.0f
#define VOXEL_OPACITY_SAND_BASALT   1.0f
#define VOXEL_OPACITY_SAND_GRANITE  1.0f
#define VOXEL_OPACITY_SAND_LIMESTONE 1.0f
#define VOXEL_OPACITY_SAND_SANDSTONE 1.0f
#define VOXEL_OPACITY_SOIL          1.0f
#define VOXEL_OPACITY_SOIL_CLAY     1.0f
#define VOXEL_OPACITY_SOIL_LOAM     1.0f
#define VOXEL_OPACITY_SOIL_SILT     1.0f
#define VOXEL_OPACITY_GRASS         1.0f
#define VOXEL_OPACITY_GRASS_WIDE    1.0f
#define VOXEL_OPACITY_GRASS_SHARP   1.0f
#define VOXEL_OPACITY_GRASS_CLOVER  1.0f
#define VOXEL_OPACITY_GRASS_MOSS    1.0f
// Tufts with sky between the blades, so light gets through where a ground block would stop it.
#define VOXEL_OPACITY_GRASS_TALL    0.35f
#define VOXEL_OPACITY_CANDLE        1.0f
#define VOXEL_OPACITY_CAMPFIRE      1.0f
#define VOXEL_OPACITY_PLASTIC        1.0f
#define VOXEL_OPACITY_CLOTH          1.0f
#define VOXEL_OPACITY_PLANK          1.0f
#define VOXEL_OPACITY_THATCH         1.0f
#define VOXEL_OPACITY_STRAW          0.85f
#define VOXEL_OPACITY_COBBLE         1.0f
#define VOXEL_OPACITY_PLASTER        1.0f
#define VOXEL_OPACITY_TERRACOTTA     1.0f
#define VOXEL_OPACITY_ADOBE          1.0f
#define VOXEL_OPACITY_GLASS_WHITE    0.45f
#define VOXEL_OPACITY_GLASS_RED      0.35f
#define VOXEL_OPACITY_GLASS_GREEN    0.35f
#define VOXEL_OPACITY_GLASS_BLUE     0.35f
#define VOXEL_OPACITY_GLASS_YELLOW   0.35f
#define VOXEL_OPACITY_WOOL_WHITE     1.0f
#define VOXEL_OPACITY_WOOL_BLACK     1.0f
#define VOXEL_OPACITY_WOOL_BROWN     1.0f
#define VOXEL_OPACITY_WOOL_GRAY      1.0f
#define VOXEL_OPACITY_WOOL_RED       1.0f
#define VOXEL_OPACITY_WOOL_BLUE      1.0f
#define VOXEL_OPACITY_WOOL_GREEN     1.0f
#define VOXEL_OPACITY_WOOL_YELLOW    1.0f
#define VOXEL_OPACITY_LEATHER        1.0f
#define VOXEL_OPACITY_FUR            1.0f
#define VOXEL_OPACITY_FEATHER        0.9f
#define VOXEL_OPACITY_SCALE          1.0f
#define VOXEL_OPACITY_SHELL          1.0f
#define VOXEL_OPACITY_HORN           1.0f
#define VOXEL_OPACITY_PAPER          1.0f
#define VOXEL_OPACITY_ROPE           1.0f
#define VOXEL_OPACITY_CERAMIC        1.0f
#define VOXEL_OPACITY_RUBBER         1.0f
#define VOXEL_OPACITY_WAX            1.0f
#define VOXEL_OPACITY_ASH            0.95f
#define VOXEL_OPACITY_DOOR           0.85f
#define VOXEL_OPACITY_ROOF_TILE      1.0f
#define VOXEL_OPACITY_CRATE          1.0f
#define VOXEL_OPACITY_BARREL         1.0f
#define VOXEL_OPACITY_BED            1.0f
#define VOXEL_OPACITY_DOOR_NS        0.85f
#define VOXEL_OPACITY_THATCH_MIRROR  0.9f
#define VOXEL_OPACITY_ROOF_TILE_MIRROR 1.0f
#define VOXEL_OPACITY_GLASS_NS       0.3f
#define VOXEL_OPACITY_STAIR          1.0f
#define VOXEL_OPACITY_STAIR_NS       1.0f
#define VOXEL_OPACITY_CHAIR          1.0f
#define VOXEL_OPACITY_TABLE          1.0f
#define VOXEL_OPACITY_CHEST          1.0f
#define VOXEL_OPACITY_FENCE          0.7f
#define VOXEL_OPACITY_FENCE_NS       0.7f
#define VOXEL_OPACITY_FENCE_WATTLE   0.65f
#define VOXEL_OPACITY_FENCE_IRON     0.75f
#define VOXEL_OPACITY_RAMPART        1.0f
#define VOXEL_OPACITY_PARAPET        1.0f
#define VOXEL_OPACITY_CRAFTING_TABLE 1.0f
#define VOXEL_OPACITY_ANVIL          1.0f
#define VOXEL_OPACITY_FORGE          1.0f
#define VOXEL_OPACITY_BUSH          0.85f
#define VOXEL_OPACITY_BUSH_FERN     0.85f
#define VOXEL_OPACITY_BUSH_VINES    0.85f
#define VOXEL_OPACITY_BUSH_THORNS   0.85f
#define VOXEL_OPACITY_BUSH_BLUEBERRY 0.85f
#define VOXEL_OPACITY_BUSH_BLACKBERRY 0.85f
#define VOXEL_OPACITY_BUSH_RASPBERRY 0.85f
#define VOXEL_OPACITY_BUSH_STRAWBERRY 0.85f
#define VOXEL_OPACITY_WOOD          1.0f
#define VOXEL_OPACITY_WOOD_OAK      1.0f
#define VOXEL_OPACITY_WOOD_BEECH    1.0f
#define VOXEL_OPACITY_WOOD_BIRCH    1.0f
#define VOXEL_OPACITY_WOOD_PINE     1.0f
#define VOXEL_OPACITY_WOOD_PECAN    1.0f
#define VOXEL_OPACITY_WOOD_LOCUST   1.0f
#define VOXEL_OPACITY_WOOD_MAPLE    1.0f
#define VOXEL_OPACITY_WOOD_ELM      1.0f
#define VOXEL_OPACITY_WOOD_HAZELNUT 1.0f
#define VOXEL_OPACITY_WOOD_CHESTNUT 1.0f
#define VOXEL_OPACITY_WOOD_WILLOW   1.0f
#define VOXEL_OPACITY_WOOD_WALNUT   1.0f
#define VOXEL_OPACITY_WOOD_ACACIA   1.0f
#define VOXEL_OPACITY_WOOD_COTTONWOOD 1.0f
#define VOXEL_OPACITY_WOOD_CYPRESS  1.0f
#define VOXEL_OPACITY_WOOD_SPRUCE   1.0f
#define VOXEL_OPACITY_WOOD_JUNIPER  1.0f
#define VOXEL_OPACITY_WOOD_REDWOOD  1.0f
#define VOXEL_OPACITY_LEAVES        0.9f
#define VOXEL_OPACITY_LEAVES_OAK    0.9f
#define VOXEL_OPACITY_LEAVES_BEECH  0.9f
#define VOXEL_OPACITY_LEAVES_BIRCH  0.9f
#define VOXEL_OPACITY_LEAVES_PINE   0.9f
#define VOXEL_OPACITY_LEAVES_PECAN  0.9f
#define VOXEL_OPACITY_LEAVES_LOCUST 0.9f
#define VOXEL_OPACITY_LEAVES_MAPLE  0.9f
#define VOXEL_OPACITY_LEAVES_ELM    0.9f
#define VOXEL_OPACITY_LEAVES_HAZELNUT 0.9f
#define VOXEL_OPACITY_LEAVES_CHESTNUT 0.9f
#define VOXEL_OPACITY_LEAVES_WILLOW 0.9f
#define VOXEL_OPACITY_LEAVES_WALNUT 0.9f
#define VOXEL_OPACITY_LEAVES_ACACIA 0.9f
#define VOXEL_OPACITY_LEAVES_COTTONWOOD 0.9f
#define VOXEL_OPACITY_LEAVES_CYPRESS 0.9f
#define VOXEL_OPACITY_LEAVES_SPRUCE 0.9f
#define VOXEL_OPACITY_LEAVES_JUNIPER 0.9f
#define VOXEL_OPACITY_LEAVES_REDWOOD 0.9f
#define VOXEL_OPACITY_ORE           1.0f
#define VOXEL_OPACITY_ORE_COAL      1.0f
#define VOXEL_OPACITY_ORE_ADAMANTITE 1.0f
#define VOXEL_OPACITY_ORE_HEMATITE  1.0f
#define VOXEL_OPACITY_ORE_MITHRIL   1.0f
#define VOXEL_OPACITY_ORE_COPPER    1.0f
#define VOXEL_OPACITY_ORE_SILVER    1.0f
#define VOXEL_OPACITY_ORE_GOLD      1.0f
#define VOXEL_OPACITY_ORE_TIN       1.0f
#define VOXEL_OPACITY_ORE_IRON      1.0f
#define VOXEL_OPACITY_ORE_LEAD      1.0f
#define VOXEL_OPACITY_ORE_ZINC      1.0f
#define VOXEL_OPACITY_ORE_TITANIUM  1.0f
#define VOXEL_OPACITY_ORE_ALUMINUM  1.0f
#define VOXEL_OPACITY_ORE_MAGNESIUM 1.0f
#define VOXEL_OPACITY_ORE_COBALT    1.0f
#define VOXEL_OPACITY_ORE_NICKEL    1.0f
#define VOXEL_OPACITY_ORE_PLATINUM  1.0f
#define VOXEL_OPACITY_ADAMANTITE    1.0f
#define VOXEL_OPACITY_HEMATITE      1.0f
#define VOXEL_OPACITY_MITHRIL       1.0f
#define VOXEL_OPACITY_COPPER        1.0f
#define VOXEL_OPACITY_SILVER        1.0f
#define VOXEL_OPACITY_GOLD          1.0f
#define VOXEL_OPACITY_TIN           1.0f
#define VOXEL_OPACITY_IRON          1.0f
#define VOXEL_OPACITY_LEAD          1.0f
#define VOXEL_OPACITY_ZINC          1.0f
#define VOXEL_OPACITY_STEEL         1.0f
#define VOXEL_OPACITY_TITANIUM      1.0f
#define VOXEL_OPACITY_ALUMINUM      1.0f
#define VOXEL_OPACITY_MAGNESIUM     1.0f
#define VOXEL_OPACITY_COBALT        1.0f
#define VOXEL_OPACITY_NICKEL        1.0f
#define VOXEL_OPACITY_PLATINUM      1.0f
#define VOXEL_OPACITY_WATER         0.8f
#define VOXEL_OPACITY_MAGMA         1.0f
#define VOXEL_OPACITY_STEAM         0.1f
#define VOXEL_OPACITY_OIL           0.9f
#define VOXEL_OPACITY_GAS           0.0f
#define VOXEL_OPACITY_SPRING        0.8f
#define VOXEL_OPACITY_SPRING_WATER  0.8f
#define VOXEL_OPACITY_SPRING_MAGMA  1.0f
#define VOXEL_OPACITY_SPRING_STEAM  0.1f
#define VOXEL_OPACITY_SPRING_OIL    0.9f
#define VOXEL_OPACITY_SPRING_GAS    0.0f
#define VOXEL_OPACITY_CRYSTAL       0.7f
#define VOXEL_OPACITY_CRYSTAL_RED   0.7f
#define VOXEL_OPACITY_CRYSTAL_GREEN 0.7f
#define VOXEL_OPACITY_CRYSTAL_BLUE  0.7f
#define VOXEL_OPACITY_BONE          1.0f
#define VOXEL_OPACITY_FLESH         1.0f
#define VOXEL_OPACITY_ORGAN         1.0f
#define VOXEL_OPACITY_BLOOD         0.8f
#define VOXEL_OPACITY_BRAIN         1.0f
#define VOXEL_OPACITY_FUNGUS        1.0f
#define VOXEL_OPACITY_GLASS         0.3f
#define VOXEL_OPACITY_BRICK         1.0f
#define VOXEL_OPACITY_LIMESTONE     1.0f
#define VOXEL_OPACITY_OBSIDIAN      1.0f
#define VOXEL_OPACITY_CLAY          1.0f
#define VOXEL_OPACITY_WOOL          1.0f
#define VOXEL_OPACITY_SNOW          0.9f
#define VOXEL_OPACITY_ICE           0.8f
#define VOXEL_OPACITY_WORLD         1.0f

// ============================================================================
// VOXEL RARITY RANKINGS
// ============================================================================

// Rarity ranking for palette sorting (lower rank = more common)
#define VOXEL_RARITY_AIR            0
#define VOXEL_RARITY_SOIL           1
#define VOXEL_RARITY_SOIL_CLAY      1
#define VOXEL_RARITY_SOIL_LOAM      1
#define VOXEL_RARITY_SOIL_SILT      1
#define VOXEL_RARITY_GRASS          2
#define VOXEL_RARITY_GRASS_WIDE     2
#define VOXEL_RARITY_GRASS_SHARP    2
#define VOXEL_RARITY_GRASS_CLOVER   2
#define VOXEL_RARITY_GRASS_MOSS     2
#define VOXEL_RARITY_GRASS_TALL     2
#define VOXEL_RARITY_CANDLE         8
#define VOXEL_RARITY_CAMPFIRE       7
#define VOXEL_RARITY_PLASTIC        18
#define VOXEL_RARITY_CLOTH          8
#define VOXEL_RARITY_PLANK          4
#define VOXEL_RARITY_THATCH         5
#define VOXEL_RARITY_STRAW          3
#define VOXEL_RARITY_COBBLE         4
#define VOXEL_RARITY_PLASTER        5
#define VOXEL_RARITY_TERRACOTTA     6
#define VOXEL_RARITY_ADOBE          4
#define VOXEL_RARITY_GLASS_WHITE    14
#define VOXEL_RARITY_GLASS_RED      16
#define VOXEL_RARITY_GLASS_GREEN    16
#define VOXEL_RARITY_GLASS_BLUE     16
#define VOXEL_RARITY_GLASS_YELLOW   16
#define VOXEL_RARITY_WOOL_WHITE     12
#define VOXEL_RARITY_WOOL_BLACK     12
#define VOXEL_RARITY_WOOL_BROWN     12
#define VOXEL_RARITY_WOOL_GRAY      12
#define VOXEL_RARITY_WOOL_RED       13
#define VOXEL_RARITY_WOOL_BLUE      13
#define VOXEL_RARITY_WOOL_GREEN     13
#define VOXEL_RARITY_WOOL_YELLOW    13
#define VOXEL_RARITY_LEATHER        10
#define VOXEL_RARITY_FUR            10
#define VOXEL_RARITY_FEATHER        9
#define VOXEL_RARITY_SCALE          11
#define VOXEL_RARITY_SHELL          11
#define VOXEL_RARITY_HORN           12
#define VOXEL_RARITY_PAPER          8
#define VOXEL_RARITY_ROPE           6
#define VOXEL_RARITY_CERAMIC        8
#define VOXEL_RARITY_RUBBER         15
#define VOXEL_RARITY_WAX            7
#define VOXEL_RARITY_ASH            2
#define VOXEL_RARITY_DOOR           6
#define VOXEL_RARITY_ROOF_TILE      6
#define VOXEL_RARITY_CRATE          5
#define VOXEL_RARITY_BARREL         5
#define VOXEL_RARITY_BED            5
#define VOXEL_RARITY_DOOR_NS        6
#define VOXEL_RARITY_THATCH_MIRROR  4
#define VOXEL_RARITY_ROOF_TILE_MIRROR 6
#define VOXEL_RARITY_GLASS_NS       8
#define VOXEL_RARITY_STAIR          5
#define VOXEL_RARITY_STAIR_NS       5
#define VOXEL_RARITY_CHAIR          5
#define VOXEL_RARITY_TABLE          5
#define VOXEL_RARITY_CHEST          6
#define VOXEL_RARITY_FENCE          4
#define VOXEL_RARITY_FENCE_NS       4
#define VOXEL_RARITY_FENCE_WATTLE   3
#define VOXEL_RARITY_FENCE_IRON     8
#define VOXEL_RARITY_RAMPART        7
#define VOXEL_RARITY_PARAPET        7
#define VOXEL_RARITY_CRAFTING_TABLE 6
#define VOXEL_RARITY_ANVIL          8
#define VOXEL_RARITY_FORGE          7
#define VOXEL_RARITY_STONE          3
#define VOXEL_RARITY_STONE_BASALT   3
#define VOXEL_RARITY_STONE_GRANITE  3
#define VOXEL_RARITY_STONE_LIMESTONE 3
#define VOXEL_RARITY_STONE_SANDSTONE 3
#define VOXEL_RARITY_GRAVEL         3
#define VOXEL_RARITY_GRAVEL_BASALT  3
#define VOXEL_RARITY_GRAVEL_GRANITE 3
#define VOXEL_RARITY_GRAVEL_LIMESTONE 3
#define VOXEL_RARITY_GRAVEL_SANDSTONE 3
#define VOXEL_RARITY_SAND           5
#define VOXEL_RARITY_SAND_BASALT    5
#define VOXEL_RARITY_SAND_GRANITE   5
#define VOXEL_RARITY_SAND_LIMESTONE 5
#define VOXEL_RARITY_SAND_SANDSTONE 5
#define VOXEL_RARITY_WOOD           4
#define VOXEL_RARITY_WOOD_OAK       4
#define VOXEL_RARITY_WOOD_BEECH     4
#define VOXEL_RARITY_WOOD_BIRCH     4
#define VOXEL_RARITY_WOOD_PINE      4
#define VOXEL_RARITY_WOOD_PECAN     4
#define VOXEL_RARITY_WOOD_LOCUST    4
#define VOXEL_RARITY_WOOD_MAPLE     4
#define VOXEL_RARITY_WOOD_ELM       4
#define VOXEL_RARITY_WOOD_HAZELNUT  4
#define VOXEL_RARITY_WOOD_CHESTNUT  4
#define VOXEL_RARITY_WOOD_WILLOW    4
#define VOXEL_RARITY_WOOD_WALNUT    4
#define VOXEL_RARITY_WOOD_ACACIA    4
#define VOXEL_RARITY_WOOD_COTTONWOOD 4
#define VOXEL_RARITY_WOOD_CYPRESS   4
#define VOXEL_RARITY_WOOD_SPRUCE    4
#define VOXEL_RARITY_WOOD_JUNIPER   4
#define VOXEL_RARITY_WOOD_REDWOOD   4
#define VOXEL_RARITY_LEAVES         5
#define VOXEL_RARITY_LEAVES_OAK     5
#define VOXEL_RARITY_LEAVES_BEECH   5
#define VOXEL_RARITY_LEAVES_BIRCH   5
#define VOXEL_RARITY_LEAVES_PINE    5
#define VOXEL_RARITY_LEAVES_PECAN   5
#define VOXEL_RARITY_LEAVES_LOCUST  5
#define VOXEL_RARITY_LEAVES_MAPLE   5
#define VOXEL_RARITY_LEAVES_ELM     5
#define VOXEL_RARITY_LEAVES_HAZELNUT 5
#define VOXEL_RARITY_LEAVES_CHESTNUT 5
#define VOXEL_RARITY_LEAVES_WILLOW  5
#define VOXEL_RARITY_LEAVES_WALNUT  5
#define VOXEL_RARITY_LEAVES_ACACIA  5
#define VOXEL_RARITY_LEAVES_COTTONWOOD 5
#define VOXEL_RARITY_LEAVES_CYPRESS 5
#define VOXEL_RARITY_LEAVES_SPRUCE  5
#define VOXEL_RARITY_LEAVES_JUNIPER 5
#define VOXEL_RARITY_LEAVES_REDWOOD 5
#define VOXEL_RARITY_BUSH           6
#define VOXEL_RARITY_BUSH_FERN      6
#define VOXEL_RARITY_BUSH_VINES     6
#define VOXEL_RARITY_BUSH_THORNS    6
#define VOXEL_RARITY_BUSH_BLUEBERRY 6
#define VOXEL_RARITY_BUSH_BLACKBERRY 6
#define VOXEL_RARITY_BUSH_RASPBERRY 6
#define VOXEL_RARITY_BUSH_STRAWBERRY 6
#define VOXEL_RARITY_ORE_COPPER     7
#define VOXEL_RARITY_ORE_SILVER     7
#define VOXEL_RARITY_ORE_GOLD       7
#define VOXEL_RARITY_ORE_TIN        7
#define VOXEL_RARITY_ORE_IRON       7
#define VOXEL_RARITY_ORE_LEAD       7
#define VOXEL_RARITY_ORE_ZINC       7
#define VOXEL_RARITY_ORE_TITANIUM   7
#define VOXEL_RARITY_ORE_ALUMINUM   7
#define VOXEL_RARITY_ORE_MAGNESIUM  7
#define VOXEL_RARITY_ORE_COBALT     7
#define VOXEL_RARITY_ORE_NICKEL     7
#define VOXEL_RARITY_ORE_PLATINUM   7
#define VOXEL_RARITY_ORE_COAL       7
#define VOXEL_RARITY_ORE_ADAMANTITE 7
#define VOXEL_RARITY_ORE_HEMATITE   7
#define VOXEL_RARITY_ORE_MITHRIL    7
#define VOXEL_RARITY_CRYSTAL        8
#define VOXEL_RARITY_CRYSTAL_RED    8
#define VOXEL_RARITY_CRYSTAL_GREEN  8
#define VOXEL_RARITY_CRYSTAL_BLUE   8
#define VOXEL_RARITY_COPPER         9
#define VOXEL_RARITY_SILVER         9
#define VOXEL_RARITY_GOLD           9
#define VOXEL_RARITY_TIN            9
#define VOXEL_RARITY_IRON           9
#define VOXEL_RARITY_LEAD           9
#define VOXEL_RARITY_ZINC           9
#define VOXEL_RARITY_STEEL          9
#define VOXEL_RARITY_TITANIUM       9
#define VOXEL_RARITY_ALUMINUM       9
#define VOXEL_RARITY_MAGNESIUM      9
#define VOXEL_RARITY_COBALT         9
#define VOXEL_RARITY_NICKEL         9
#define VOXEL_RARITY_PLATINUM       9
#define VOXEL_RARITY_ADAMANTITE     9
#define VOXEL_RARITY_HEMATITE       9
#define VOXEL_RARITY_MITHRIL        9
#define VOXEL_RARITY_BEDROCK        10
#define VOXEL_RARITY_SPRING         11
#define VOXEL_RARITY_SPRING_WATER   11
#define VOXEL_RARITY_SPRING_MAGMA   11
#define VOXEL_RARITY_SPRING_STEAM   11
#define VOXEL_RARITY_SPRING_OIL     11
#define VOXEL_RARITY_SPRING_GAS     11
#define VOXEL_RARITY_WATER          12
#define VOXEL_RARITY_MAGMA          12
#define VOXEL_RARITY_STEAM          12
#define VOXEL_RARITY_OIL            12
#define VOXEL_RARITY_GAS            12
#define VOXEL_RARITY_BONE           12
#define VOXEL_RARITY_FLESH          12
#define VOXEL_RARITY_ORGAN          12
#define VOXEL_RARITY_BLOOD          12
#define VOXEL_RARITY_BRAIN          12
#define VOXEL_RARITY_FUNGUS         12
#define VOXEL_RARITY_GLASS          12
#define VOXEL_RARITY_BRICK          12
#define VOXEL_RARITY_LIMESTONE      12
#define VOXEL_RARITY_OBSIDIAN       12
#define VOXEL_RARITY_CLAY           12
#define VOXEL_RARITY_WOOL           12
#define VOXEL_RARITY_SNOW           12
#define VOXEL_RARITY_ICE            12
#define VOXEL_RARITY_ACTOR          12
#define VOXEL_RARITY_ORE            12
#define VOXEL_RARITY_WORLD          12

// Voxel structure - standardized interface
typedef struct
{
  VoxelType type;          // Voxel type, can be 0-255 (VOXEL_WORLD) which references another world
  uint64_t condition_mask; // Bitmask of known conditions (constants-defined)
  // Packed auxiliary data (8 x 8-bit fields). Each field is 0..255.
  // Field layout:
  //  - 0: true entropy (0..255) — invariant per voxel content for RNG-less hashing/FX
  //  - 1: quantity (fluids amount or generic quantity)
  //  - 2: damage (generic damage accumulation)
  //  - 3: temperature (0..255)
  //  - 4: heat (editor/physics heat level 0..6)
  //  - 5: shape×orient pack (see voxel_shape.h) — 0 = full cube
  //  - 6: decoration yaw (0..255 → 0..2π) for nested silhouette rotation
  //  - 7: reserved
  uint64_t data8;
  // New physics-oriented fields (runtime-only; not serialized yet):
  // Rotation stored as Euler angles in radians: rx, ry, rz
  float rotation[3];
  // Momentum vector (vx, vy, vz) in arbitrary units
  float momentum[3];
} Voxel;

// Field indices for data8 packed fields
typedef enum
{
  VOXEL_FIELD_ENTROPY = 0,
  VOXEL_FIELD_QUANTITY = 1,
  VOXEL_FIELD_DAMAGE = 2,
  VOXEL_FIELD_TEMPERATURE = 3,
  VOXEL_FIELD_HEAT = 4, // TODO: replace usage with TEMPERATURE, restore to RESERVED_4
  VOXEL_FIELD_SHAPE_ORIENT = 5, // VoxelShape (bits 0..2) + orientation (bits 3..7)
  VOXEL_FIELD_YAW = 6,          // decoration yaw byte (0..255 → 0..2π)
  VOXEL_FIELD_RESERVED_7 = 7
} VoxelFieldIndex;

// Physical constants
#define VOXEL_METERS_PER_SIDE 0.06f // TODO: use 1m for now, 1/32m later
#define VOXEL_VOLUME_M3 (VOXEL_METERS_PER_SIDE * VOXEL_METERS_PER_SIDE * VOXEL_METERS_PER_SIDE)

// ============================================================================
// VOXEL TYPE PROPERTIES
// ============================================================================

// Get human-readable name for voxel type (implemented inline below)
// const char *voxel_type_name(VoxelType type);

// Get RGB color for voxel type (implemented inline below)
// void voxel_type_color(VoxelType type, uint8_t *r, uint8_t *g, uint8_t *b);

// Low-grass turf coats (VOXEL_GRASS .. VOXEL_GRASS_MOSS). Tall grass is a separate sparse prop.

static inline bool voxel_type_is_wool(VoxelType type)
{
  return type == VOXEL_WOOL || (type >= VOXEL_WOOL_WHITE && type <= VOXEL_WOOL_YELLOW);
}

static inline bool voxel_type_is_glass(VoxelType type)
{
  return type == VOXEL_GLASS || type == VOXEL_GLASS_NS ||
         (type >= VOXEL_GLASS_WHITE && type <= VOXEL_GLASS_YELLOW);
}

static inline bool voxel_type_is_construction(VoxelType type)
{
  return type == VOXEL_BRICK || type == VOXEL_PLANK || type == VOXEL_THATCH ||
         type == VOXEL_COBBLE || type == VOXEL_PLASTER || type == VOXEL_TERRACOTTA ||
         type == VOXEL_ADOBE || type == VOXEL_CERAMIC || type == VOXEL_DOOR ||
         type == VOXEL_DOOR_NS || type == VOXEL_ROOF_TILE || type == VOXEL_ROOF_TILE_MIRROR ||
         type == VOXEL_THATCH_MIRROR || type == VOXEL_CRATE || type == VOXEL_BARREL ||
         type == VOXEL_BED || type == VOXEL_GLASS_NS || type == VOXEL_STAIR ||
         type == VOXEL_STAIR_NS || type == VOXEL_CHAIR || type == VOXEL_TABLE ||
         type == VOXEL_CHEST || type == VOXEL_FENCE || type == VOXEL_FENCE_NS ||
         type == VOXEL_FENCE_WATTLE || type == VOXEL_FENCE_IRON || type == VOXEL_RAMPART ||
         type == VOXEL_PARAPET || type == VOXEL_CRAFTING_TABLE || type == VOXEL_ANVIL ||
         type == VOXEL_FORGE;
}
static inline bool voxel_type_is_low_grass(VoxelType type)
{
  return type >= VOXEL_GRASS && type <= VOXEL_GRASS_MOSS;
}

static inline bool voxel_type_is_bush(VoxelType type)
{
  return type >= VOXEL_BUSH && type <= VOXEL_BUSH_STRAWBERRY;
}

static inline bool voxel_type_is_leaves(VoxelType type)
{
  return type >= VOXEL_LEAVES && type <= VOXEL_LEAVES_REDWOOD;
}

// Soft vegetation for tip impostors / roughness: turf, tall grass, bushes, canopy.
static inline bool voxel_type_is_vegetation(VoxelType type)
{
  return voxel_type_is_low_grass(type) || type == VOXEL_GRASS_TALL || voxel_type_is_bush(type) ||
         voxel_type_is_leaves(type);
}

// What remains after breaking a voxel: low-grass scrapes to soil; everything else becomes air.
static inline VoxelType voxel_type_after_break(VoxelType type)
{
  if (voxel_type_is_low_grass(type))
    return VOXEL_SOIL;
  return VOXEL_AIR;
}

// Get mass (kg) for voxel type - implemented as static inline below

// Check if voxel type is solid
bool voxel_type_is_solid(VoxelType type);

// Check if voxel type is fluid
bool voxel_type_is_fluid(VoxelType type);

// Check if voxel type is gas
bool voxel_type_is_gas(VoxelType type);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Get voxel mass in kg
float voxel_get_mass_kg(const Voxel *voxel);

// Get voxel density in kg/m³
float voxel_get_density_kg_m3(const Voxel *voxel);

// Create a hash of voxel contents for comparison
uint64_t voxel_hash(const Voxel *voxel);

// Convert voxel to string representation (for debugging)
char *voxel_to_string(const Voxel *voxel);

// Parse voxel from string representation
bool voxel_from_string(Voxel *voxel, const char *str);

// ============================================================================
// WORLD INTEGRATION FUNCTIONS
// ============================================================================
// Note: World integration functions are defined in world.h to avoid circular dependencies

// Apply damage to voxel (returns true if voxel should be destroyed)
bool voxel_apply_damage(Voxel *voxel, uint8_t damage_amount);

// Heat up voxel (affects temperature and may cause state changes)
void voxel_heat(Voxel *voxel, uint8_t heat_amount);

// Cool down voxel (affects temperature and may cause state changes).
// Unset temperature (0) on non-ice materials is treated as VOXEL_TEMP_AMBIENT so
// cooling has room to run. When temperature reaches 0, VOXEL_WATER freezes to VOXEL_ICE.
// Returns true if this call froze water into ice.
bool voxel_cool(Voxel *voxel, uint8_t cool_amount);

// Room-temperature baseline used when a voxel has never been heated or cooled.
#define VOXEL_TEMP_AMBIENT 128u

// ============================================================================
// INLINE IMPLEMENTATIONS
// ============================================================================

// Inline implementations for performance-critical functions
static inline void voxel_init(Voxel *voxel)
{
  if (!voxel) return;
  voxel->type = VOXEL_AIR;
  voxel->condition_mask = 0ULL;
  voxel->data8 = 0ULL;
  voxel->rotation[0] = voxel->rotation[1] = voxel->rotation[2] = 0.0f;
  voxel->momentum[0] = voxel->momentum[1] = voxel->momentum[2] = 0.0f;
}

static inline void voxel_init_with_type(Voxel *voxel, VoxelType type)
{
  if (!voxel) return;
  voxel_init(voxel);
  voxel->type = type;
}

static inline VoxelType voxel_get_type(const Voxel *voxel)
{
  return voxel ? voxel->type : VOXEL_AIR;
}

static inline void voxel_set_type(Voxel *voxel, VoxelType type)
{
  if (voxel) voxel->type = type;
}

static inline bool voxel_is_solid(const Voxel *voxel)
{
  return voxel && voxel->type != VOXEL_AIR;
}

static inline bool voxel_is_air(const Voxel *voxel)
{
  return !voxel || voxel->type == VOXEL_AIR;
}

static inline uint8_t voxel_get_field(const Voxel *voxel, VoxelFieldIndex index)
{
  if (!voxel || index < 0 || index > 7)
    return 0;
  return (uint8_t)((voxel->data8 >> (index * 8)) & 0xFFULL);
}

static inline void voxel_set_field(Voxel *voxel, VoxelFieldIndex index, uint8_t value)
{
  if (!voxel || index < 0 || index > 7)
    return;
  uint64_t mask = 0xFFULL << (index * 8);
  voxel->data8 = (voxel->data8 & ~mask) | (((uint64_t)value & 0xFFULL) << (index * 8));
}

static inline uint8_t voxel_get_entropy(const Voxel *voxel)
{
  return voxel_get_field(voxel, VOXEL_FIELD_ENTROPY);
}

static inline void voxel_set_entropy(Voxel *voxel, uint8_t entropy)
{
  voxel_set_field(voxel, VOXEL_FIELD_ENTROPY, entropy);
}

static inline uint8_t voxel_get_quantity(const Voxel *voxel)
{
  return voxel_get_field(voxel, VOXEL_FIELD_QUANTITY);
}

static inline void voxel_set_quantity(Voxel *voxel, uint8_t quantity)
{
  voxel_set_field(voxel, VOXEL_FIELD_QUANTITY, quantity);
}

static inline uint8_t voxel_get_damage(const Voxel *voxel)
{
  return voxel_get_field(voxel, VOXEL_FIELD_DAMAGE);
}

static inline void voxel_set_damage(Voxel *voxel, uint8_t damage)
{
  voxel_set_field(voxel, VOXEL_FIELD_DAMAGE, damage);
}

static inline uint8_t voxel_get_temperature(const Voxel *voxel)
{
  return voxel_get_field(voxel, VOXEL_FIELD_TEMPERATURE);
}

static inline void voxel_set_temperature(Voxel *voxel, uint8_t temperature)
{
  voxel_set_field(voxel, VOXEL_FIELD_TEMPERATURE, temperature);
}

static inline uint8_t voxel_get_heat(const Voxel *voxel)
{
  return voxel_get_field(voxel, VOXEL_FIELD_HEAT);
}

static inline void voxel_set_heat(Voxel *voxel, uint8_t heat)
{
  voxel_set_field(voxel, VOXEL_FIELD_HEAT, heat);
}

static inline uint64_t voxel_get_condition_mask(const Voxel *voxel)
{
  return voxel ? voxel->condition_mask : 0ULL;
}

static inline void voxel_set_condition_mask(Voxel *voxel, uint64_t mask)
{
  if (voxel) voxel->condition_mask = mask;
}

static inline void voxel_add_condition(Voxel *voxel, uint64_t condition_bit)
{
  if (voxel) voxel->condition_mask |= condition_bit;
}

static inline void voxel_remove_condition(Voxel *voxel, uint64_t condition_bit)
{
  if (voxel) voxel->condition_mask &= ~condition_bit;
}

static inline bool voxel_has_condition(const Voxel *voxel, uint64_t condition_bit)
{
  return voxel && (voxel->condition_mask & condition_bit) != 0ULL;
}

static inline void voxel_clear_conditions(Voxel *voxel)
{
  if (voxel) voxel->condition_mask = 0ULL;
}

static inline void voxel_get_rotation(const Voxel *voxel, float *rx, float *ry, float *rz)
{
  if (!voxel) return;
  if (rx) *rx = voxel->rotation[0];
  if (ry) *ry = voxel->rotation[1];
  if (rz) *rz = voxel->rotation[2];
}

static inline void voxel_set_rotation(Voxel *voxel, float rx, float ry, float rz)
{
  if (!voxel) return;
  voxel->rotation[0] = rx;
  voxel->rotation[1] = ry;
  voxel->rotation[2] = rz;
}

static inline void voxel_get_momentum(const Voxel *voxel, float *vx, float *vy, float *vz)
{
  if (!voxel) return;
  if (vx) *vx = voxel->momentum[0];
  if (vy) *vy = voxel->momentum[1];
  if (vz) *vz = voxel->momentum[2];
}

static inline void voxel_set_momentum(Voxel *voxel, float vx, float vy, float vz)
{
  if (!voxel) return;
  voxel->momentum[0] = vx;
  voxel->momentum[1] = vy;
  voxel->momentum[2] = vz;
}

static inline void voxel_reset_physics(Voxel *voxel)
{
  if (!voxel) return;
  voxel->rotation[0] = voxel->rotation[1] = voxel->rotation[2] = 0.0f;
  voxel->momentum[0] = voxel->momentum[1] = voxel->momentum[2] = 0.0f;
}

static inline float voxel_get_volume_m3(const Voxel *voxel)
{
  return VOXEL_VOLUME_M3;
}

static inline bool voxel_equals(const Voxel *a, const Voxel *b)
{
  if (!a || !b) return a == b;
  return a->type == b->type &&
         a->condition_mask == b->condition_mask &&
         a->data8 == b->data8 &&
         a->rotation[0] == b->rotation[0] &&
         a->rotation[1] == b->rotation[1] &&
         a->rotation[2] == b->rotation[2] &&
         a->momentum[0] == b->momentum[0] &&
         a->momentum[1] == b->momentum[1] &&
         a->momentum[2] == b->momentum[2];
}

static inline void voxel_copy(Voxel *dest, const Voxel *src)
{
  if (!dest || !src) return;
  *dest = *src;
}

static inline bool voxel_is_valid(const Voxel *voxel)
{
  return voxel != NULL;
}

static inline void voxel_create_water(Voxel *voxel, uint8_t quantity)
{
  if (!voxel) return;
  voxel_init_with_type(voxel, VOXEL_WATER);
  voxel_set_quantity(voxel, quantity);
}

static inline void voxel_create_fluid(Voxel *voxel, VoxelType fluid_type, uint8_t quantity)
{
  if (!voxel) return;
  voxel_init_with_type(voxel, fluid_type);
  voxel_set_quantity(voxel, quantity);
}

static inline void voxel_create_solid(Voxel *voxel, VoxelType solid_type, uint8_t damage)
{
  if (!voxel) return;
  voxel_init_with_type(voxel, solid_type);
  voxel_set_damage(voxel, damage);
}

static inline void voxel_create_gas(Voxel *voxel, VoxelType gas_type, uint8_t temperature)
{
  if (!voxel) return;
  voxel_init_with_type(voxel, gas_type);
  voxel_set_temperature(voxel, temperature);
}

// ============================================================================
// IMPLEMENTATIONS FOR VOXEL TYPE PROPERTIES
// ============================================================================

// Forward declarations for world functions
const char *world_voxel_type_name(VoxelType type);
void world_voxel_type_color(VoxelType type, uint8_t *r, uint8_t *g, uint8_t *b);

// Note: World struct is defined in world.h

// ============================================================================
// CANONICAL VOXEL PROPERTY ACCESS FUNCTIONS
// ============================================================================

// Get mass (kg) for a voxel type
static inline float voxel_type_mass_kg(VoxelType type)
{
  return (type >= 0 && type < VOXEL_COUNT) ? g_voxel_type_mass_kg[type] : 0.0f;
}

// Get opacity (0.0 = transparent, 1.0 = opaque) for a voxel type
static inline float voxel_type_opacity(VoxelType type)
{
  switch (type)
  {
  case VOXEL_AIR: return VOXEL_OPACITY_AIR;
  case VOXEL_BEDROCK: return VOXEL_OPACITY_BEDROCK;
  case VOXEL_ACTOR: return VOXEL_OPACITY_ACTOR;
  case VOXEL_STONE: return VOXEL_OPACITY_STONE;
  case VOXEL_STONE_BASALT: return VOXEL_OPACITY_STONE_BASALT;
  case VOXEL_STONE_GRANITE: return VOXEL_OPACITY_STONE_GRANITE;
  case VOXEL_STONE_LIMESTONE: return VOXEL_OPACITY_STONE_LIMESTONE;
  case VOXEL_STONE_SANDSTONE: return VOXEL_OPACITY_STONE_SANDSTONE;
  case VOXEL_GRAVEL: return VOXEL_OPACITY_GRAVEL;
  case VOXEL_GRAVEL_BASALT: return VOXEL_OPACITY_GRAVEL_BASALT;
  case VOXEL_GRAVEL_GRANITE: return VOXEL_OPACITY_GRAVEL_GRANITE;
  case VOXEL_GRAVEL_LIMESTONE: return VOXEL_OPACITY_GRAVEL_LIMESTONE;
  case VOXEL_GRAVEL_SANDSTONE: return VOXEL_OPACITY_GRAVEL_SANDSTONE;
  case VOXEL_SAND: return VOXEL_OPACITY_SAND;
  case VOXEL_SAND_BASALT: return VOXEL_OPACITY_SAND_BASALT;
  case VOXEL_SAND_GRANITE: return VOXEL_OPACITY_SAND_GRANITE;
  case VOXEL_SAND_LIMESTONE: return VOXEL_OPACITY_SAND_LIMESTONE;
  case VOXEL_SAND_SANDSTONE: return VOXEL_OPACITY_SAND_SANDSTONE;
  case VOXEL_SOIL: return VOXEL_OPACITY_SOIL;
  case VOXEL_SOIL_CLAY: return VOXEL_OPACITY_SOIL_CLAY;
  case VOXEL_SOIL_LOAM: return VOXEL_OPACITY_SOIL_LOAM;
  case VOXEL_SOIL_SILT: return VOXEL_OPACITY_SOIL_SILT;
  case VOXEL_GRASS: return VOXEL_OPACITY_GRASS;
  case VOXEL_GRASS_WIDE: return VOXEL_OPACITY_GRASS_WIDE;
  case VOXEL_GRASS_SHARP: return VOXEL_OPACITY_GRASS_SHARP;
  case VOXEL_GRASS_CLOVER: return VOXEL_OPACITY_GRASS_CLOVER;
  case VOXEL_GRASS_MOSS: return VOXEL_OPACITY_GRASS_MOSS;
  case VOXEL_GRASS_TALL: return VOXEL_OPACITY_GRASS_TALL;
  case VOXEL_CANDLE: return VOXEL_OPACITY_CANDLE;
  case VOXEL_CAMPFIRE: return VOXEL_OPACITY_CAMPFIRE;
  case VOXEL_PLASTIC: return VOXEL_OPACITY_PLASTIC;
  case VOXEL_CLOTH: return VOXEL_OPACITY_CLOTH;
  case VOXEL_PLANK: return VOXEL_OPACITY_PLANK;
  case VOXEL_THATCH: return VOXEL_OPACITY_THATCH;
  case VOXEL_STRAW: return VOXEL_OPACITY_STRAW;
  case VOXEL_COBBLE: return VOXEL_OPACITY_COBBLE;
  case VOXEL_PLASTER: return VOXEL_OPACITY_PLASTER;
  case VOXEL_TERRACOTTA: return VOXEL_OPACITY_TERRACOTTA;
  case VOXEL_ADOBE: return VOXEL_OPACITY_ADOBE;
  case VOXEL_GLASS_WHITE: return VOXEL_OPACITY_GLASS_WHITE;
  case VOXEL_GLASS_RED: return VOXEL_OPACITY_GLASS_RED;
  case VOXEL_GLASS_GREEN: return VOXEL_OPACITY_GLASS_GREEN;
  case VOXEL_GLASS_BLUE: return VOXEL_OPACITY_GLASS_BLUE;
  case VOXEL_GLASS_YELLOW: return VOXEL_OPACITY_GLASS_YELLOW;
  case VOXEL_WOOL_WHITE: return VOXEL_OPACITY_WOOL_WHITE;
  case VOXEL_WOOL_BLACK: return VOXEL_OPACITY_WOOL_BLACK;
  case VOXEL_WOOL_BROWN: return VOXEL_OPACITY_WOOL_BROWN;
  case VOXEL_WOOL_GRAY: return VOXEL_OPACITY_WOOL_GRAY;
  case VOXEL_WOOL_RED: return VOXEL_OPACITY_WOOL_RED;
  case VOXEL_WOOL_BLUE: return VOXEL_OPACITY_WOOL_BLUE;
  case VOXEL_WOOL_GREEN: return VOXEL_OPACITY_WOOL_GREEN;
  case VOXEL_WOOL_YELLOW: return VOXEL_OPACITY_WOOL_YELLOW;
  case VOXEL_LEATHER: return VOXEL_OPACITY_LEATHER;
  case VOXEL_FUR: return VOXEL_OPACITY_FUR;
  case VOXEL_FEATHER: return VOXEL_OPACITY_FEATHER;
  case VOXEL_SCALE: return VOXEL_OPACITY_SCALE;
  case VOXEL_SHELL: return VOXEL_OPACITY_SHELL;
  case VOXEL_HORN: return VOXEL_OPACITY_HORN;
  case VOXEL_PAPER: return VOXEL_OPACITY_PAPER;
  case VOXEL_ROPE: return VOXEL_OPACITY_ROPE;
  case VOXEL_CERAMIC: return VOXEL_OPACITY_CERAMIC;
  case VOXEL_RUBBER: return VOXEL_OPACITY_RUBBER;
  case VOXEL_WAX: return VOXEL_OPACITY_WAX;
  case VOXEL_ASH: return VOXEL_OPACITY_ASH;
  case VOXEL_DOOR: return VOXEL_OPACITY_DOOR;
  case VOXEL_ROOF_TILE: return VOXEL_OPACITY_ROOF_TILE;
  case VOXEL_CRATE: return VOXEL_OPACITY_CRATE;
  case VOXEL_BARREL: return VOXEL_OPACITY_BARREL;
  case VOXEL_BED: return VOXEL_OPACITY_BED;
  case VOXEL_DOOR_NS: return VOXEL_OPACITY_DOOR_NS;
  case VOXEL_THATCH_MIRROR: return VOXEL_OPACITY_THATCH_MIRROR;
  case VOXEL_ROOF_TILE_MIRROR: return VOXEL_OPACITY_ROOF_TILE_MIRROR;
  case VOXEL_GLASS_NS: return VOXEL_OPACITY_GLASS_NS;
  case VOXEL_STAIR: return VOXEL_OPACITY_STAIR;
  case VOXEL_STAIR_NS: return VOXEL_OPACITY_STAIR_NS;
  case VOXEL_CHAIR: return VOXEL_OPACITY_CHAIR;
  case VOXEL_TABLE: return VOXEL_OPACITY_TABLE;
  case VOXEL_CHEST: return VOXEL_OPACITY_CHEST;
  case VOXEL_FENCE: return VOXEL_OPACITY_FENCE;
  case VOXEL_FENCE_NS: return VOXEL_OPACITY_FENCE_NS;
  case VOXEL_FENCE_WATTLE: return VOXEL_OPACITY_FENCE_WATTLE;
  case VOXEL_FENCE_IRON: return VOXEL_OPACITY_FENCE_IRON;
  case VOXEL_RAMPART: return VOXEL_OPACITY_RAMPART;
  case VOXEL_PARAPET: return VOXEL_OPACITY_PARAPET;
  case VOXEL_CRAFTING_TABLE: return VOXEL_OPACITY_CRAFTING_TABLE;
  case VOXEL_ANVIL: return VOXEL_OPACITY_ANVIL;
  case VOXEL_FORGE: return VOXEL_OPACITY_FORGE;
  case VOXEL_BUSH: return VOXEL_OPACITY_BUSH;
  case VOXEL_BUSH_FERN: return VOXEL_OPACITY_BUSH_FERN;
  case VOXEL_BUSH_VINES: return VOXEL_OPACITY_BUSH_VINES;
  case VOXEL_BUSH_THORNS: return VOXEL_OPACITY_BUSH_THORNS;
  case VOXEL_BUSH_BLUEBERRY: return VOXEL_OPACITY_BUSH_BLUEBERRY;
  case VOXEL_BUSH_BLACKBERRY: return VOXEL_OPACITY_BUSH_BLACKBERRY;
  case VOXEL_BUSH_RASPBERRY: return VOXEL_OPACITY_BUSH_RASPBERRY;
  case VOXEL_BUSH_STRAWBERRY: return VOXEL_OPACITY_BUSH_STRAWBERRY;
  case VOXEL_WOOD: return VOXEL_OPACITY_WOOD;
  case VOXEL_WOOD_OAK: return VOXEL_OPACITY_WOOD_OAK;
  case VOXEL_WOOD_BEECH: return VOXEL_OPACITY_WOOD_BEECH;
  case VOXEL_WOOD_BIRCH: return VOXEL_OPACITY_WOOD_BIRCH;
  case VOXEL_WOOD_PINE: return VOXEL_OPACITY_WOOD_PINE;
  case VOXEL_WOOD_PECAN: return VOXEL_OPACITY_WOOD_PECAN;
  case VOXEL_WOOD_LOCUST: return VOXEL_OPACITY_WOOD_LOCUST;
  case VOXEL_WOOD_MAPLE: return VOXEL_OPACITY_WOOD_MAPLE;
  case VOXEL_WOOD_ELM: return VOXEL_OPACITY_WOOD_ELM;
  case VOXEL_WOOD_HAZELNUT: return VOXEL_OPACITY_WOOD_HAZELNUT;
  case VOXEL_WOOD_CHESTNUT: return VOXEL_OPACITY_WOOD_CHESTNUT;
  case VOXEL_WOOD_WILLOW: return VOXEL_OPACITY_WOOD_WILLOW;
  case VOXEL_WOOD_WALNUT: return VOXEL_OPACITY_WOOD_WALNUT;
  case VOXEL_WOOD_ACACIA: return VOXEL_OPACITY_WOOD_ACACIA;
  case VOXEL_WOOD_COTTONWOOD: return VOXEL_OPACITY_WOOD_COTTONWOOD;
  case VOXEL_WOOD_CYPRESS: return VOXEL_OPACITY_WOOD_CYPRESS;
  case VOXEL_WOOD_SPRUCE: return VOXEL_OPACITY_WOOD_SPRUCE;
  case VOXEL_WOOD_JUNIPER: return VOXEL_OPACITY_WOOD_JUNIPER;
  case VOXEL_WOOD_REDWOOD: return VOXEL_OPACITY_WOOD_REDWOOD;
  case VOXEL_LEAVES: return VOXEL_OPACITY_LEAVES;
  case VOXEL_LEAVES_OAK: return VOXEL_OPACITY_LEAVES_OAK;
  case VOXEL_LEAVES_BEECH: return VOXEL_OPACITY_LEAVES_BEECH;
  case VOXEL_LEAVES_BIRCH: return VOXEL_OPACITY_LEAVES_BIRCH;
  case VOXEL_LEAVES_PINE: return VOXEL_OPACITY_LEAVES_PINE;
  case VOXEL_LEAVES_PECAN: return VOXEL_OPACITY_LEAVES_PECAN;
  case VOXEL_LEAVES_LOCUST: return VOXEL_OPACITY_LEAVES_LOCUST;
  case VOXEL_LEAVES_MAPLE: return VOXEL_OPACITY_LEAVES_MAPLE;
  case VOXEL_LEAVES_ELM: return VOXEL_OPACITY_LEAVES_ELM;
  case VOXEL_LEAVES_HAZELNUT: return VOXEL_OPACITY_LEAVES_HAZELNUT;
  case VOXEL_LEAVES_CHESTNUT: return VOXEL_OPACITY_LEAVES_CHESTNUT;
  case VOXEL_LEAVES_WILLOW: return VOXEL_OPACITY_LEAVES_WILLOW;
  case VOXEL_LEAVES_WALNUT: return VOXEL_OPACITY_LEAVES_WALNUT;
  case VOXEL_LEAVES_ACACIA: return VOXEL_OPACITY_LEAVES_ACACIA;
  case VOXEL_LEAVES_COTTONWOOD: return VOXEL_OPACITY_LEAVES_COTTONWOOD;
  case VOXEL_LEAVES_CYPRESS: return VOXEL_OPACITY_LEAVES_CYPRESS;
  case VOXEL_LEAVES_SPRUCE: return VOXEL_OPACITY_LEAVES_SPRUCE;
  case VOXEL_LEAVES_JUNIPER: return VOXEL_OPACITY_LEAVES_JUNIPER;
  case VOXEL_LEAVES_REDWOOD: return VOXEL_OPACITY_LEAVES_REDWOOD;
  case VOXEL_ORE: return VOXEL_OPACITY_ORE;
  case VOXEL_ORE_COAL: return VOXEL_OPACITY_ORE_COAL;
  case VOXEL_ORE_ADAMANTITE: return VOXEL_OPACITY_ORE_ADAMANTITE;
  case VOXEL_ORE_HEMATITE: return VOXEL_OPACITY_ORE_HEMATITE;
  case VOXEL_ORE_MITHRIL: return VOXEL_OPACITY_ORE_MITHRIL;
  case VOXEL_ORE_COPPER: return VOXEL_OPACITY_ORE_COPPER;
  case VOXEL_ORE_SILVER: return VOXEL_OPACITY_ORE_SILVER;
  case VOXEL_ORE_GOLD: return VOXEL_OPACITY_ORE_GOLD;
  case VOXEL_ORE_TIN: return VOXEL_OPACITY_ORE_TIN;
  case VOXEL_ORE_IRON: return VOXEL_OPACITY_ORE_IRON;
  case VOXEL_ORE_LEAD: return VOXEL_OPACITY_ORE_LEAD;
  case VOXEL_ORE_ZINC: return VOXEL_OPACITY_ORE_ZINC;
  case VOXEL_ORE_TITANIUM: return VOXEL_OPACITY_ORE_TITANIUM;
  case VOXEL_ORE_ALUMINUM: return VOXEL_OPACITY_ORE_ALUMINUM;
  case VOXEL_ORE_MAGNESIUM: return VOXEL_OPACITY_ORE_MAGNESIUM;
  case VOXEL_ORE_COBALT: return VOXEL_OPACITY_ORE_COBALT;
  case VOXEL_ORE_NICKEL: return VOXEL_OPACITY_ORE_NICKEL;
  case VOXEL_ORE_PLATINUM: return VOXEL_OPACITY_ORE_PLATINUM;
  case VOXEL_ADAMANTITE: return VOXEL_OPACITY_ADAMANTITE;
  case VOXEL_HEMATITE: return VOXEL_OPACITY_HEMATITE;
  case VOXEL_MITHRIL: return VOXEL_OPACITY_MITHRIL;
  case VOXEL_COPPER: return VOXEL_OPACITY_COPPER;
  case VOXEL_SILVER: return VOXEL_OPACITY_SILVER;
  case VOXEL_GOLD: return VOXEL_OPACITY_GOLD;
  case VOXEL_TIN: return VOXEL_OPACITY_TIN;
  case VOXEL_IRON: return VOXEL_OPACITY_IRON;
  case VOXEL_LEAD: return VOXEL_OPACITY_LEAD;
  case VOXEL_ZINC: return VOXEL_OPACITY_ZINC;
  case VOXEL_STEEL: return VOXEL_OPACITY_STEEL;
  case VOXEL_TITANIUM: return VOXEL_OPACITY_TITANIUM;
  case VOXEL_ALUMINUM: return VOXEL_OPACITY_ALUMINUM;
  case VOXEL_MAGNESIUM: return VOXEL_OPACITY_MAGNESIUM;
  case VOXEL_COBALT: return VOXEL_OPACITY_COBALT;
  case VOXEL_NICKEL: return VOXEL_OPACITY_NICKEL;
  case VOXEL_PLATINUM: return VOXEL_OPACITY_PLATINUM;
  case VOXEL_WATER: return VOXEL_OPACITY_WATER;
  case VOXEL_MAGMA: return VOXEL_OPACITY_MAGMA;
  case VOXEL_STEAM: return VOXEL_OPACITY_STEAM;
  case VOXEL_OIL: return VOXEL_OPACITY_OIL;
  case VOXEL_GAS: return VOXEL_OPACITY_GAS;
  case VOXEL_SPRING: return VOXEL_OPACITY_SPRING;
  case VOXEL_SPRING_WATER: return VOXEL_OPACITY_SPRING_WATER;
  case VOXEL_SPRING_MAGMA: return VOXEL_OPACITY_SPRING_MAGMA;
  case VOXEL_SPRING_STEAM: return VOXEL_OPACITY_SPRING_STEAM;
  case VOXEL_SPRING_OIL: return VOXEL_OPACITY_SPRING_OIL;
  case VOXEL_SPRING_GAS: return VOXEL_OPACITY_SPRING_GAS;
  case VOXEL_CRYSTAL: return VOXEL_OPACITY_CRYSTAL;
  case VOXEL_CRYSTAL_RED: return VOXEL_OPACITY_CRYSTAL_RED;
  case VOXEL_CRYSTAL_GREEN: return VOXEL_OPACITY_CRYSTAL_GREEN;
  case VOXEL_CRYSTAL_BLUE: return VOXEL_OPACITY_CRYSTAL_BLUE;
  case VOXEL_BONE: return VOXEL_OPACITY_BONE;
  case VOXEL_FLESH: return VOXEL_OPACITY_FLESH;
  case VOXEL_ORGAN: return VOXEL_OPACITY_ORGAN;
  case VOXEL_BLOOD: return VOXEL_OPACITY_BLOOD;
  case VOXEL_BRAIN: return VOXEL_OPACITY_BRAIN;
  case VOXEL_FUNGUS: return VOXEL_OPACITY_FUNGUS;
  case VOXEL_GLASS: return VOXEL_OPACITY_GLASS;
  case VOXEL_BRICK: return VOXEL_OPACITY_BRICK;
  case VOXEL_LIMESTONE: return VOXEL_OPACITY_LIMESTONE;
  case VOXEL_OBSIDIAN: return VOXEL_OPACITY_OBSIDIAN;
  case VOXEL_CLAY: return VOXEL_OPACITY_CLAY;
  case VOXEL_WOOL: return VOXEL_OPACITY_WOOL;
  case VOXEL_SNOW: return VOXEL_OPACITY_SNOW;
  case VOXEL_ICE: return VOXEL_OPACITY_ICE;
  case VOXEL_WORLD: return VOXEL_OPACITY_WORLD;
  default: return 1.0f; // Default to opaque
  }
}

// Get rarity ranking (lower rank = more common) for a voxel type
static inline int voxel_type_rarity(VoxelType type)
{
  switch (type)
  {
  case VOXEL_AIR: return VOXEL_RARITY_AIR;
  case VOXEL_SOIL: return VOXEL_RARITY_SOIL;
  case VOXEL_SOIL_CLAY: return VOXEL_RARITY_SOIL_CLAY;
  case VOXEL_SOIL_LOAM: return VOXEL_RARITY_SOIL_LOAM;
  case VOXEL_SOIL_SILT: return VOXEL_RARITY_SOIL_SILT;
  case VOXEL_GRASS: return VOXEL_RARITY_GRASS;
  case VOXEL_GRASS_WIDE: return VOXEL_RARITY_GRASS_WIDE;
  case VOXEL_GRASS_SHARP: return VOXEL_RARITY_GRASS_SHARP;
  case VOXEL_GRASS_CLOVER: return VOXEL_RARITY_GRASS_CLOVER;
  case VOXEL_GRASS_MOSS: return VOXEL_RARITY_GRASS_MOSS;
  case VOXEL_GRASS_TALL: return VOXEL_RARITY_GRASS_TALL;
  case VOXEL_CANDLE: return VOXEL_RARITY_CANDLE;
  case VOXEL_CAMPFIRE: return VOXEL_RARITY_CAMPFIRE;
  case VOXEL_PLASTIC: return VOXEL_RARITY_PLASTIC;
  case VOXEL_CLOTH: return VOXEL_RARITY_CLOTH;
  case VOXEL_PLANK: return VOXEL_RARITY_PLANK;
  case VOXEL_THATCH: return VOXEL_RARITY_THATCH;
  case VOXEL_STRAW: return VOXEL_RARITY_STRAW;
  case VOXEL_COBBLE: return VOXEL_RARITY_COBBLE;
  case VOXEL_PLASTER: return VOXEL_RARITY_PLASTER;
  case VOXEL_TERRACOTTA: return VOXEL_RARITY_TERRACOTTA;
  case VOXEL_ADOBE: return VOXEL_RARITY_ADOBE;
  case VOXEL_GLASS_WHITE: return VOXEL_RARITY_GLASS_WHITE;
  case VOXEL_GLASS_RED: return VOXEL_RARITY_GLASS_RED;
  case VOXEL_GLASS_GREEN: return VOXEL_RARITY_GLASS_GREEN;
  case VOXEL_GLASS_BLUE: return VOXEL_RARITY_GLASS_BLUE;
  case VOXEL_GLASS_YELLOW: return VOXEL_RARITY_GLASS_YELLOW;
  case VOXEL_WOOL_WHITE: return VOXEL_RARITY_WOOL_WHITE;
  case VOXEL_WOOL_BLACK: return VOXEL_RARITY_WOOL_BLACK;
  case VOXEL_WOOL_BROWN: return VOXEL_RARITY_WOOL_BROWN;
  case VOXEL_WOOL_GRAY: return VOXEL_RARITY_WOOL_GRAY;
  case VOXEL_WOOL_RED: return VOXEL_RARITY_WOOL_RED;
  case VOXEL_WOOL_BLUE: return VOXEL_RARITY_WOOL_BLUE;
  case VOXEL_WOOL_GREEN: return VOXEL_RARITY_WOOL_GREEN;
  case VOXEL_WOOL_YELLOW: return VOXEL_RARITY_WOOL_YELLOW;
  case VOXEL_LEATHER: return VOXEL_RARITY_LEATHER;
  case VOXEL_FUR: return VOXEL_RARITY_FUR;
  case VOXEL_FEATHER: return VOXEL_RARITY_FEATHER;
  case VOXEL_SCALE: return VOXEL_RARITY_SCALE;
  case VOXEL_SHELL: return VOXEL_RARITY_SHELL;
  case VOXEL_HORN: return VOXEL_RARITY_HORN;
  case VOXEL_PAPER: return VOXEL_RARITY_PAPER;
  case VOXEL_ROPE: return VOXEL_RARITY_ROPE;
  case VOXEL_CERAMIC: return VOXEL_RARITY_CERAMIC;
  case VOXEL_RUBBER: return VOXEL_RARITY_RUBBER;
  case VOXEL_WAX: return VOXEL_RARITY_WAX;
  case VOXEL_ASH: return VOXEL_RARITY_ASH;
  case VOXEL_DOOR: return VOXEL_RARITY_DOOR;
  case VOXEL_ROOF_TILE: return VOXEL_RARITY_ROOF_TILE;
  case VOXEL_CRATE: return VOXEL_RARITY_CRATE;
  case VOXEL_BARREL: return VOXEL_RARITY_BARREL;
  case VOXEL_BED: return VOXEL_RARITY_BED;
  case VOXEL_DOOR_NS: return VOXEL_RARITY_DOOR_NS;
  case VOXEL_THATCH_MIRROR: return VOXEL_RARITY_THATCH_MIRROR;
  case VOXEL_ROOF_TILE_MIRROR: return VOXEL_RARITY_ROOF_TILE_MIRROR;
  case VOXEL_GLASS_NS: return VOXEL_RARITY_GLASS_NS;
  case VOXEL_STAIR: return VOXEL_RARITY_STAIR;
  case VOXEL_STAIR_NS: return VOXEL_RARITY_STAIR_NS;
  case VOXEL_CHAIR: return VOXEL_RARITY_CHAIR;
  case VOXEL_TABLE: return VOXEL_RARITY_TABLE;
  case VOXEL_CHEST: return VOXEL_RARITY_CHEST;
  case VOXEL_FENCE: return VOXEL_RARITY_FENCE;
  case VOXEL_FENCE_NS: return VOXEL_RARITY_FENCE_NS;
  case VOXEL_FENCE_WATTLE: return VOXEL_RARITY_FENCE_WATTLE;
  case VOXEL_FENCE_IRON: return VOXEL_RARITY_FENCE_IRON;
  case VOXEL_RAMPART: return VOXEL_RARITY_RAMPART;
  case VOXEL_PARAPET: return VOXEL_RARITY_PARAPET;
  case VOXEL_CRAFTING_TABLE: return VOXEL_RARITY_CRAFTING_TABLE;
  case VOXEL_ANVIL: return VOXEL_RARITY_ANVIL;
  case VOXEL_FORGE: return VOXEL_RARITY_FORGE;
  case VOXEL_STONE: return VOXEL_RARITY_STONE;
  case VOXEL_STONE_BASALT: return VOXEL_RARITY_STONE_BASALT;
  case VOXEL_STONE_GRANITE: return VOXEL_RARITY_STONE_GRANITE;
  case VOXEL_STONE_LIMESTONE: return VOXEL_RARITY_STONE_LIMESTONE;
  case VOXEL_STONE_SANDSTONE: return VOXEL_RARITY_STONE_SANDSTONE;
  case VOXEL_GRAVEL: return VOXEL_RARITY_GRAVEL;
  case VOXEL_GRAVEL_BASALT: return VOXEL_RARITY_GRAVEL_BASALT;
  case VOXEL_GRAVEL_GRANITE: return VOXEL_RARITY_GRAVEL_GRANITE;
  case VOXEL_GRAVEL_LIMESTONE: return VOXEL_RARITY_GRAVEL_LIMESTONE;
  case VOXEL_GRAVEL_SANDSTONE: return VOXEL_RARITY_GRAVEL_SANDSTONE;
  case VOXEL_SAND: return VOXEL_RARITY_SAND;
  case VOXEL_SAND_BASALT: return VOXEL_RARITY_SAND_BASALT;
  case VOXEL_SAND_GRANITE: return VOXEL_RARITY_SAND_GRANITE;
  case VOXEL_SAND_LIMESTONE: return VOXEL_RARITY_SAND_LIMESTONE;
  case VOXEL_SAND_SANDSTONE: return VOXEL_RARITY_SAND_SANDSTONE;
  case VOXEL_WOOD: return VOXEL_RARITY_WOOD;
  case VOXEL_WOOD_OAK: return VOXEL_RARITY_WOOD_OAK;
  case VOXEL_WOOD_BEECH: return VOXEL_RARITY_WOOD_BEECH;
  case VOXEL_WOOD_BIRCH: return VOXEL_RARITY_WOOD_BIRCH;
  case VOXEL_WOOD_PINE: return VOXEL_RARITY_WOOD_PINE;
  case VOXEL_WOOD_PECAN: return VOXEL_RARITY_WOOD_PECAN;
  case VOXEL_WOOD_LOCUST: return VOXEL_RARITY_WOOD_LOCUST;
  case VOXEL_WOOD_MAPLE: return VOXEL_RARITY_WOOD_MAPLE;
  case VOXEL_WOOD_ELM: return VOXEL_RARITY_WOOD_ELM;
  case VOXEL_WOOD_HAZELNUT: return VOXEL_RARITY_WOOD_HAZELNUT;
  case VOXEL_WOOD_CHESTNUT: return VOXEL_RARITY_WOOD_CHESTNUT;
  case VOXEL_WOOD_WILLOW: return VOXEL_RARITY_WOOD_WILLOW;
  case VOXEL_WOOD_WALNUT: return VOXEL_RARITY_WOOD_WALNUT;
  case VOXEL_WOOD_ACACIA: return VOXEL_RARITY_WOOD_ACACIA;
  case VOXEL_WOOD_COTTONWOOD: return VOXEL_RARITY_WOOD_COTTONWOOD;
  case VOXEL_WOOD_CYPRESS: return VOXEL_RARITY_WOOD_CYPRESS;
  case VOXEL_WOOD_SPRUCE: return VOXEL_RARITY_WOOD_SPRUCE;
  case VOXEL_WOOD_JUNIPER: return VOXEL_RARITY_WOOD_JUNIPER;
  case VOXEL_WOOD_REDWOOD: return VOXEL_RARITY_WOOD_REDWOOD;
  case VOXEL_LEAVES: return VOXEL_RARITY_LEAVES;
  case VOXEL_LEAVES_OAK: return VOXEL_RARITY_LEAVES_OAK;
  case VOXEL_LEAVES_BEECH: return VOXEL_RARITY_LEAVES_BEECH;
  case VOXEL_LEAVES_BIRCH: return VOXEL_RARITY_LEAVES_BIRCH;
  case VOXEL_LEAVES_PINE: return VOXEL_RARITY_LEAVES_PINE;
  case VOXEL_LEAVES_PECAN: return VOXEL_RARITY_LEAVES_PECAN;
  case VOXEL_LEAVES_LOCUST: return VOXEL_RARITY_LEAVES_LOCUST;
  case VOXEL_LEAVES_MAPLE: return VOXEL_RARITY_LEAVES_MAPLE;
  case VOXEL_LEAVES_ELM: return VOXEL_RARITY_LEAVES_ELM;
  case VOXEL_LEAVES_HAZELNUT: return VOXEL_RARITY_LEAVES_HAZELNUT;
  case VOXEL_LEAVES_CHESTNUT: return VOXEL_RARITY_LEAVES_CHESTNUT;
  case VOXEL_LEAVES_WILLOW: return VOXEL_RARITY_LEAVES_WILLOW;
  case VOXEL_LEAVES_WALNUT: return VOXEL_RARITY_LEAVES_WALNUT;
  case VOXEL_LEAVES_ACACIA: return VOXEL_RARITY_LEAVES_ACACIA;
  case VOXEL_LEAVES_COTTONWOOD: return VOXEL_RARITY_LEAVES_COTTONWOOD;
  case VOXEL_LEAVES_CYPRESS: return VOXEL_RARITY_LEAVES_CYPRESS;
  case VOXEL_LEAVES_SPRUCE: return VOXEL_RARITY_LEAVES_SPRUCE;
  case VOXEL_LEAVES_JUNIPER: return VOXEL_RARITY_LEAVES_JUNIPER;
  case VOXEL_LEAVES_REDWOOD: return VOXEL_RARITY_LEAVES_REDWOOD;
  case VOXEL_BUSH: return VOXEL_RARITY_BUSH;
  case VOXEL_BUSH_FERN: return VOXEL_RARITY_BUSH_FERN;
  case VOXEL_BUSH_VINES: return VOXEL_RARITY_BUSH_VINES;
  case VOXEL_BUSH_THORNS: return VOXEL_RARITY_BUSH_THORNS;
  case VOXEL_BUSH_BLUEBERRY: return VOXEL_RARITY_BUSH_BLUEBERRY;
  case VOXEL_BUSH_BLACKBERRY: return VOXEL_RARITY_BUSH_BLACKBERRY;
  case VOXEL_BUSH_RASPBERRY: return VOXEL_RARITY_BUSH_RASPBERRY;
  case VOXEL_BUSH_STRAWBERRY: return VOXEL_RARITY_BUSH_STRAWBERRY;
  case VOXEL_ORE_COPPER: return VOXEL_RARITY_ORE_COPPER;
  case VOXEL_ORE_SILVER: return VOXEL_RARITY_ORE_SILVER;
  case VOXEL_ORE_GOLD: return VOXEL_RARITY_ORE_GOLD;
  case VOXEL_ORE_TIN: return VOXEL_RARITY_ORE_TIN;
  case VOXEL_ORE_IRON: return VOXEL_RARITY_ORE_IRON;
  case VOXEL_ORE_LEAD: return VOXEL_RARITY_ORE_LEAD;
  case VOXEL_ORE_ZINC: return VOXEL_RARITY_ORE_ZINC;
  case VOXEL_ORE_TITANIUM: return VOXEL_RARITY_ORE_TITANIUM;
  case VOXEL_ORE_ALUMINUM: return VOXEL_RARITY_ORE_ALUMINUM;
  case VOXEL_ORE_MAGNESIUM: return VOXEL_RARITY_ORE_MAGNESIUM;
  case VOXEL_ORE_COBALT: return VOXEL_RARITY_ORE_COBALT;
  case VOXEL_ORE_NICKEL: return VOXEL_RARITY_ORE_NICKEL;
  case VOXEL_ORE_PLATINUM: return VOXEL_RARITY_ORE_PLATINUM;
  case VOXEL_ORE_COAL: return VOXEL_RARITY_ORE_COAL;
  case VOXEL_ORE_ADAMANTITE: return VOXEL_RARITY_ORE_ADAMANTITE;
  case VOXEL_ORE_HEMATITE: return VOXEL_RARITY_ORE_HEMATITE;
  case VOXEL_ORE_MITHRIL: return VOXEL_RARITY_ORE_MITHRIL;
  case VOXEL_CRYSTAL: return VOXEL_RARITY_CRYSTAL;
  case VOXEL_CRYSTAL_RED: return VOXEL_RARITY_CRYSTAL_RED;
  case VOXEL_CRYSTAL_GREEN: return VOXEL_RARITY_CRYSTAL_GREEN;
  case VOXEL_CRYSTAL_BLUE: return VOXEL_RARITY_CRYSTAL_BLUE;
  case VOXEL_COPPER: return VOXEL_RARITY_COPPER;
  case VOXEL_SILVER: return VOXEL_RARITY_SILVER;
  case VOXEL_GOLD: return VOXEL_RARITY_GOLD;
  case VOXEL_TIN: return VOXEL_RARITY_TIN;
  case VOXEL_IRON: return VOXEL_RARITY_IRON;
  case VOXEL_LEAD: return VOXEL_RARITY_LEAD;
  case VOXEL_ZINC: return VOXEL_RARITY_ZINC;
  case VOXEL_STEEL: return VOXEL_RARITY_STEEL;
  case VOXEL_TITANIUM: return VOXEL_RARITY_TITANIUM;
  case VOXEL_ALUMINUM: return VOXEL_RARITY_ALUMINUM;
  case VOXEL_MAGNESIUM: return VOXEL_RARITY_MAGNESIUM;
  case VOXEL_COBALT: return VOXEL_RARITY_COBALT;
  case VOXEL_NICKEL: return VOXEL_RARITY_NICKEL;
  case VOXEL_PLATINUM: return VOXEL_RARITY_PLATINUM;
  case VOXEL_ADAMANTITE: return VOXEL_RARITY_ADAMANTITE;
  case VOXEL_HEMATITE: return VOXEL_RARITY_HEMATITE;
  case VOXEL_MITHRIL: return VOXEL_RARITY_MITHRIL;
  case VOXEL_BEDROCK: return VOXEL_RARITY_BEDROCK;
  case VOXEL_SPRING: return VOXEL_RARITY_SPRING;
  case VOXEL_SPRING_WATER: return VOXEL_RARITY_SPRING_WATER;
  case VOXEL_SPRING_MAGMA: return VOXEL_RARITY_SPRING_MAGMA;
  case VOXEL_SPRING_STEAM: return VOXEL_RARITY_SPRING_STEAM;
  case VOXEL_SPRING_OIL: return VOXEL_RARITY_SPRING_OIL;
  case VOXEL_SPRING_GAS: return VOXEL_RARITY_SPRING_GAS;
  case VOXEL_WATER: return VOXEL_RARITY_WATER;
  case VOXEL_MAGMA: return VOXEL_RARITY_MAGMA;
  case VOXEL_STEAM: return VOXEL_RARITY_STEAM;
  case VOXEL_OIL: return VOXEL_RARITY_OIL;
  case VOXEL_GAS: return VOXEL_RARITY_GAS;
  case VOXEL_BONE: return VOXEL_RARITY_BONE;
  case VOXEL_FLESH: return VOXEL_RARITY_FLESH;
  case VOXEL_ORGAN: return VOXEL_RARITY_ORGAN;
  case VOXEL_BLOOD: return VOXEL_RARITY_BLOOD;
  case VOXEL_BRAIN: return VOXEL_RARITY_BRAIN;
  case VOXEL_FUNGUS: return VOXEL_RARITY_FUNGUS;
  case VOXEL_GLASS: return VOXEL_RARITY_GLASS;
  case VOXEL_BRICK: return VOXEL_RARITY_BRICK;
  case VOXEL_LIMESTONE: return VOXEL_RARITY_LIMESTONE;
  case VOXEL_OBSIDIAN: return VOXEL_RARITY_OBSIDIAN;
  case VOXEL_CLAY: return VOXEL_RARITY_CLAY;
  case VOXEL_WOOL: return VOXEL_RARITY_WOOL;
  case VOXEL_SNOW: return VOXEL_RARITY_SNOW;
  case VOXEL_ICE: return VOXEL_RARITY_ICE;
  case VOXEL_ACTOR: return VOXEL_RARITY_ACTOR;
  case VOXEL_ORE: return VOXEL_RARITY_ORE;
  case VOXEL_WORLD: return VOXEL_RARITY_WORLD;
  default: return 12; // Default to most rare
  }
}

// Canonical voxel type name implementation
static inline const char *voxel_type_name(VoxelType type)
{
  switch (type)
  {
  case VOXEL_AIR:
    return "AIR";
  case VOXEL_BEDROCK:
    return "BEDROCK";
  case VOXEL_ACTOR:
    return "ACTOR";
  case VOXEL_STONE:
    return "STONE";
  case VOXEL_STONE_BASALT:
    return "STONE_BASALT";
  case VOXEL_STONE_GRANITE:
    return "STONE_GRANITE";
  case VOXEL_STONE_LIMESTONE:
    return "STONE_LIMESTONE";
  case VOXEL_STONE_SANDSTONE:
    return "STONE_SANDSTONE";
  case VOXEL_GRAVEL:
    return "GRAVEL";
  case VOXEL_GRAVEL_BASALT:
    return "GRAVEL_BASALT";
  case VOXEL_GRAVEL_GRANITE:
    return "GRAVEL_GRANITE";
  case VOXEL_GRAVEL_LIMESTONE:
    return "GRAVEL_LIMESTONE";
  case VOXEL_GRAVEL_SANDSTONE:
    return "GRAVEL_SANDSTONE";
  case VOXEL_SAND:
    return "SAND";
  case VOXEL_SAND_BASALT:
    return "SAND_BASALT";
  case VOXEL_SAND_GRANITE:
    return "SAND_GRANITE";
  case VOXEL_SAND_LIMESTONE:
    return "SAND_LIMESTONE";
  case VOXEL_SAND_SANDSTONE:
    return "SAND_SANDSTONE";
  case VOXEL_SOIL:
    return "SOIL";
  case VOXEL_SOIL_CLAY:
    return "SOIL_CLAY";
  case VOXEL_SOIL_LOAM:
    return "SOIL_LOAM";
  case VOXEL_SOIL_SILT:
    return "SOIL_SILT";
  case VOXEL_GRASS:
    return "GRASS";
  case VOXEL_GRASS_WIDE:
    return "GRASS_WIDE";
  case VOXEL_GRASS_SHARP:
    return "GRASS_SHARP";
  case VOXEL_GRASS_CLOVER:
    return "GRASS_CLOVER";
  case VOXEL_GRASS_MOSS:
    return "GRASS_MOSS";
  case VOXEL_GRASS_TALL:
    return "GRASS_TALL";
  case VOXEL_CANDLE:
    return "CANDLE";
  case VOXEL_CAMPFIRE:
    return "CAMPFIRE";
  case VOXEL_BUSH:
    return "BUSH";
  case VOXEL_BUSH_FERN:
    return "BUSH_FERN";
  case VOXEL_BUSH_VINES:
    return "BUSH_VINES";
  case VOXEL_BUSH_THORNS:
    return "BUSH_THORNS";
  case VOXEL_BUSH_BLUEBERRY:
    return "BUSH_BLUEBERRY";
  case VOXEL_BUSH_BLACKBERRY:
    return "BUSH_BLACKBERRY";
  case VOXEL_BUSH_RASPBERRY:
    return "BUSH_RASPBERRY";
  case VOXEL_BUSH_STRAWBERRY:
    return "BUSH_STRAWBERRY";
  case VOXEL_WOOD:
    return "WOOD";
  case VOXEL_WOOD_OAK:
    return "WOOD_OAK";
  case VOXEL_WOOD_BEECH:
    return "WOOD_BEECH";
  case VOXEL_WOOD_BIRCH:
    return "WOOD_BIRCH";
  case VOXEL_WOOD_PINE:
    return "WOOD_PINE";
  case VOXEL_WOOD_PECAN:
    return "WOOD_PECAN";
  case VOXEL_WOOD_LOCUST:
    return "WOOD_LOCUST";
  case VOXEL_WOOD_MAPLE:
    return "WOOD_MAPLE";
  case VOXEL_WOOD_ELM:
    return "WOOD_ELM";
  case VOXEL_WOOD_HAZELNUT:
    return "WOOD_HAZELNUT";
  case VOXEL_WOOD_CHESTNUT:
    return "WOOD_CHESTNUT";
  case VOXEL_WOOD_WILLOW:
    return "WOOD_WILLOW";
  case VOXEL_WOOD_WALNUT:
    return "WOOD_WALNUT";
  case VOXEL_WOOD_ACACIA:
    return "WOOD_ACACIA";
  case VOXEL_WOOD_COTTONWOOD:
    return "WOOD_COTTONWOOD";
  case VOXEL_WOOD_CYPRESS:
    return "WOOD_CYPRESS";
  case VOXEL_WOOD_SPRUCE:
    return "WOOD_SPRUCE";
  case VOXEL_WOOD_JUNIPER:
    return "WOOD_JUNIPER";
  case VOXEL_WOOD_REDWOOD:
    return "WOOD_REDWOOD";
  case VOXEL_LEAVES:
    return "LEAVES";
  case VOXEL_LEAVES_OAK:
    return "LEAVES_OAK";
  case VOXEL_LEAVES_BEECH:
    return "LEAVES_BEECH";
  case VOXEL_LEAVES_BIRCH:
    return "LEAVES_BIRCH";
  case VOXEL_LEAVES_PINE:
    return "LEAVES_PINE";
  case VOXEL_LEAVES_PECAN:
    return "LEAVES_PECAN";
  case VOXEL_LEAVES_LOCUST:
    return "LEAVES_LOCUST";
  case VOXEL_LEAVES_MAPLE:
    return "LEAVES_MAPLE";
  case VOXEL_LEAVES_ELM:
    return "LEAVES_ELM";
  case VOXEL_LEAVES_HAZELNUT:
    return "LEAVES_HAZELNUT";
  case VOXEL_LEAVES_CHESTNUT:
    return "LEAVES_CHESTNUT";
  case VOXEL_LEAVES_WILLOW:
    return "LEAVES_WILLOW";
  case VOXEL_LEAVES_WALNUT:
    return "LEAVES_WALNUT";
  case VOXEL_LEAVES_ACACIA:
    return "LEAVES_ACACIA";
  case VOXEL_LEAVES_COTTONWOOD:
    return "LEAVES_COTTONWOOD";
  case VOXEL_LEAVES_CYPRESS:
    return "LEAVES_CYPRESS";
  case VOXEL_LEAVES_SPRUCE:
    return "LEAVES_SPRUCE";
  case VOXEL_LEAVES_JUNIPER:
    return "LEAVES_JUNIPER";
  case VOXEL_LEAVES_REDWOOD:
    return "LEAVES_REDWOOD";
  case VOXEL_ORE:
    return "ORE";
  case VOXEL_ORE_COAL:
    return "ORE_COAL";
  case VOXEL_ORE_ADAMANTITE:
    return "ORE_ADAMANTITE";
  case VOXEL_ORE_HEMATITE:
    return "ORE_HEMATITE";
  case VOXEL_ORE_MITHRIL:
    return "ORE_MITHRIL";
  case VOXEL_ORE_COPPER:
    return "ORE_COPPER";
  case VOXEL_ORE_SILVER:
    return "ORE_SILVER";
  case VOXEL_ORE_GOLD:
    return "ORE_GOLD";
  case VOXEL_ORE_TIN:
    return "ORE_TIN";
  case VOXEL_ORE_IRON:
    return "ORE_IRON";
  case VOXEL_ORE_LEAD:
    return "ORE_LEAD";
  case VOXEL_ORE_ZINC:
    return "ORE_ZINC";
  case VOXEL_ORE_TITANIUM:
    return "ORE_TITANIUM";
  case VOXEL_ORE_ALUMINUM:
    return "ORE_ALUMINUM";
  case VOXEL_ORE_MAGNESIUM:
    return "ORE_MAGNESIUM";
  case VOXEL_ORE_COBALT:
    return "ORE_COBALT";
  case VOXEL_ORE_NICKEL:
    return "ORE_NICKEL";
  case VOXEL_ORE_PLATINUM:
    return "ORE_PLATINUM";
  case VOXEL_ADAMANTITE:
    return "ADAMANTITE";
  case VOXEL_HEMATITE:
    return "HEMATITE";
  case VOXEL_MITHRIL:
    return "MITHRIL";
  case VOXEL_COPPER:
    return "COPPER";
  case VOXEL_SILVER:
    return "SILVER";
  case VOXEL_GOLD:
    return "GOLD";
  case VOXEL_TIN:
    return "TIN";
  case VOXEL_IRON:
    return "IRON";
  case VOXEL_LEAD:
    return "LEAD";
  case VOXEL_ZINC:
    return "ZINC";
  case VOXEL_STEEL:
    return "STEEL";
  case VOXEL_TITANIUM:
    return "TITANIUM";
  case VOXEL_ALUMINUM:
    return "ALUMINUM";
  case VOXEL_MAGNESIUM:
    return "MAGNESIUM";
  case VOXEL_COBALT:
    return "COBALT";
  case VOXEL_NICKEL:
    return "NICKEL";
  case VOXEL_PLATINUM:
    return "PLATINUM";
  case VOXEL_WATER:
    return "WATER";
  case VOXEL_MAGMA:
    return "MAGMA";
  case VOXEL_STEAM:
    return "STEAM";
  case VOXEL_OIL:
    return "OIL";
  case VOXEL_GAS:
    return "GAS";
  case VOXEL_SPRING:
    return "SPRING";
  case VOXEL_SPRING_WATER:
    return "SPRING_WATER";
  case VOXEL_SPRING_MAGMA:
    return "SPRING_MAGMA";
  case VOXEL_SPRING_STEAM:
    return "SPRING_STEAM";
  case VOXEL_SPRING_OIL:
    return "SPRING_OIL";
  case VOXEL_SPRING_GAS:
    return "SPRING_GAS";
  case VOXEL_CRYSTAL:
    return "CRYSTAL";
  case VOXEL_CRYSTAL_RED:
    return "CRYSTAL_RED";
  case VOXEL_CRYSTAL_GREEN:
    return "CRYSTAL_GREEN";
  case VOXEL_CRYSTAL_BLUE:
    return "CRYSTAL_BLUE";
  case VOXEL_BONE:
    return "BONE";
  case VOXEL_FLESH:
    return "FLESH";
  case VOXEL_ORGAN:
    return "ORGAN";
  case VOXEL_BLOOD:
    return "BLOOD";
  case VOXEL_BRAIN:
    return "BRAIN";
  case VOXEL_FUNGUS:
    return "FUNGUS";
  case VOXEL_GLASS:
    return "GLASS";
  case VOXEL_BRICK:
    return "BRICK";
  case VOXEL_LIMESTONE:
    return "LIMESTONE";
  case VOXEL_OBSIDIAN:
    return "OBSIDIAN";
  case VOXEL_CLAY:
    return "CLAY";
  case VOXEL_WOOL:
    return "WOOL";
  case VOXEL_SNOW:
    return "SNOW";
  case VOXEL_ICE:
    return "ICE";
  case VOXEL_PLASTIC:
    return "PLASTIC";
  case VOXEL_CLOTH:
    return "CLOTH";
  case VOXEL_PLANK:
    return "PLANK";
  case VOXEL_THATCH:
    return "THATCH";
  case VOXEL_STRAW:
    return "STRAW";
  case VOXEL_COBBLE:
    return "COBBLE";
  case VOXEL_PLASTER:
    return "PLASTER";
  case VOXEL_TERRACOTTA:
    return "TERRACOTTA";
  case VOXEL_ADOBE:
    return "ADOBE";
  case VOXEL_GLASS_WHITE:
    return "GLASS_WHITE";
  case VOXEL_GLASS_RED:
    return "GLASS_RED";
  case VOXEL_GLASS_GREEN:
    return "GLASS_GREEN";
  case VOXEL_GLASS_BLUE:
    return "GLASS_BLUE";
  case VOXEL_GLASS_YELLOW:
    return "GLASS_YELLOW";
  case VOXEL_WOOL_WHITE:
    return "WOOL_WHITE";
  case VOXEL_WOOL_BLACK:
    return "WOOL_BLACK";
  case VOXEL_WOOL_BROWN:
    return "WOOL_BROWN";
  case VOXEL_WOOL_GRAY:
    return "WOOL_GRAY";
  case VOXEL_WOOL_RED:
    return "WOOL_RED";
  case VOXEL_WOOL_BLUE:
    return "WOOL_BLUE";
  case VOXEL_WOOL_GREEN:
    return "WOOL_GREEN";
  case VOXEL_WOOL_YELLOW:
    return "WOOL_YELLOW";
  case VOXEL_LEATHER:
    return "LEATHER";
  case VOXEL_FUR:
    return "FUR";
  case VOXEL_FEATHER:
    return "FEATHER";
  case VOXEL_SCALE:
    return "SCALE";
  case VOXEL_SHELL:
    return "SHELL";
  case VOXEL_HORN:
    return "HORN";
  case VOXEL_PAPER:
    return "PAPER";
  case VOXEL_ROPE:
    return "ROPE";
  case VOXEL_CERAMIC:
    return "CERAMIC";
  case VOXEL_RUBBER:
    return "RUBBER";
  case VOXEL_WAX:
    return "WAX";
  case VOXEL_ASH:
    return "ASH";
  case VOXEL_DOOR:
    return "DOOR";
  case VOXEL_ROOF_TILE:
    return "ROOF_TILE";
  case VOXEL_CRATE:
    return "CRATE";
  case VOXEL_BARREL:
    return "BARREL";
  case VOXEL_BED:
    return "BED";
  case VOXEL_DOOR_NS:
    return "DOOR_NS";
  case VOXEL_THATCH_MIRROR:
    return "THATCH_MIRROR";
  case VOXEL_ROOF_TILE_MIRROR:
    return "ROOF_TILE_MIRROR";
  case VOXEL_GLASS_NS:
    return "GLASS_NS";
  case VOXEL_STAIR:
    return "STAIR";
  case VOXEL_STAIR_NS:
    return "STAIR_NS";
  case VOXEL_CHAIR:
    return "CHAIR";
  case VOXEL_TABLE:
    return "TABLE";
  case VOXEL_CHEST:
    return "CHEST";
  case VOXEL_FENCE:
    return "FENCE";
  case VOXEL_FENCE_NS:
    return "FENCE_NS";
  case VOXEL_FENCE_WATTLE:
    return "FENCE_WATTLE";
  case VOXEL_FENCE_IRON:
    return "FENCE_IRON";
  case VOXEL_RAMPART:
    return "RAMPART";
  case VOXEL_PARAPET:
    return "PARAPET";
  case VOXEL_CRAFTING_TABLE:
    return "CRAFTING_TABLE";
  case VOXEL_ANVIL:
    return "ANVIL";
  case VOXEL_FORGE:
    return "FORGE";
  case VOXEL_WORLD:
    return "WORLD";
  default:
    return "UNKNOWN";
  }
}

static inline void voxel_type_color(VoxelType type, uint8_t *r, uint8_t *g, uint8_t *b)
{
  world_voxel_type_color(type, r, g, b);
}

#endif // VOXEL_H
