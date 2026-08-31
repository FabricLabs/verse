// model_transformer.c
#include "model_transformer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

// Optional: use SDL2 image loading when available for more formats
// Note: Avoid external image libs to keep lints clean. We'll implement a tiny BMP reader/writer.

// --- Utility: clamp and color mapping --------------------------------------

static inline uint8_t clamp_u8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

// Map approximate RGB to a VoxelType. This is a very simple heuristic palette.
static VoxelType map_rgb_to_voxel_type(uint8_t r, uint8_t g, uint8_t b) {
  // Special cases
  if (r == 0 && g == 0 && b == 0) return VOXEL_AIR;

  // Water-ish
  if (b > 150 && g > 100 && r < 80) return VOXEL_WATER;

  // Grass-ish
  if (g > 140 && r < 120 && b < 120) return VOXEL_GRASS;

  // Dirt/Wood-ish
  if (r > 90 && g > 50 && b < 60) return VOXEL_SOIL;

  // Leaves-ish
  if (g > 110 && r < 120) return VOXEL_LEAVES;

  // Stone-ish
  if (r > 90 && g > 90 && b > 90) return VOXEL_STONE;

  // Sand-ish
  if (r > 200 && g > 170 && b < 120) return VOXEL_SAND;

  return VOXEL_STONE;
}

// Map VoxelType to RGB color for export
static void voxel_type_to_rgb(VoxelType t, uint8_t* r, uint8_t* g, uint8_t* b) {
  switch (t) {
    case VOXEL_AIR:    *r=0;   *g=0;   *b=0;   break;
    case VOXEL_SOIL:   *r=120; *g=80;  *b=40;  break;
    case VOXEL_GRASS:  *r=90;  *g=170; *b=50;  break;
    case VOXEL_STONE:  *r=128; *g=128; *b=128; break;
    case VOXEL_WATER:  *r=30;  *g=144; *b=255; break;
    case VOXEL_WOOD:   *r=139; *g=90;  *b=43;  break;
    case VOXEL_LEAVES: *r=34;  *g=139; *b=34;  break;
    case VOXEL_SAND:   *r=238; *g=203; *b=173; break;
    case VOXEL_BEDROCK:*r=50;  *g=50;  *b=50;  break;
    case VOXEL_SPRING: *r=100; *g=200; *b=255; break;
    case VOXEL_WORLD:  *r=255; *g=0;   *b=255; break;
    default:           *r=64;  *g=64;  *b=64;  break;
  }
}

// --- Minimal MagicaVoxel VOX support ---------------------------------------

// MagicaVoxel .vox format reference: version 150
// We'll implement a simplified reader/writer that understands SIZE/XYZI/MAIN/VOX

static uint32_t read_u32_le(FILE* f) {
  uint8_t b[4];
  if (fread(b, 1, 4, f) != 4) return 0;
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint8_t read_u8(FILE* f) {
  uint8_t b; if (fread(&b, 1, 1, f) != 1) return 0; return b;
}

static void write_u32_le(FILE* f, uint32_t v) {
  uint8_t b[4] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF) };
  fwrite(b, 1, 4, f);
}

static void write_u8(FILE* f, uint8_t v) { fwrite(&v, 1, 1, f); }

// Probe MagicaVoxel .vox SIZE dimensions quickly
bool model_transformer_probe_vox_dimensions(const char* vox_filepath,
                                            uint32_t* out_size_x,
                                            uint32_t* out_size_y,
                                            uint32_t* out_size_z) {
  if (!vox_filepath || !out_size_x || !out_size_y || !out_size_z) return false;
  FILE* f = fopen(vox_filepath, "rb");
  if (!f) return false;
  char magic[4];
  if (fread(magic, 1, 4, f) != 4 || strncmp(magic, "VOX ", 4) != 0) { fclose(f); return false; }
  (void)read_u32_le(f); // version
  char chunk_id[4];
  if (fread(chunk_id, 1, 4, f) != 4 || strncmp(chunk_id, "MAIN", 4) != 0) { fclose(f); return false; }
  (void)read_u32_le(f); // main content
  (void)read_u32_le(f); // main children
  while (!feof(f)) {
    if (fread(chunk_id, 1, 4, f) != 4) break;
    uint32_t content_size = read_u32_le(f);
    uint32_t children_size = read_u32_le(f);
    long chunk_start = ftell(f);
    if (strncmp(chunk_id, "SIZE", 4) == 0) {
      *out_size_x = read_u32_le(f);
      *out_size_y = read_u32_le(f);
      *out_size_z = read_u32_le(f);
      fclose(f);
      return true;
    }
    fseek(f, chunk_start + content_size + children_size, SEEK_SET);
  }
  fclose(f);
  return false;
}


// ---------------------------------------------------------------------------
// MagicaVoxel palette → gameplay VoxelType
// ---------------------------------------------------------------------------
// Authors paint with arbitrary RGB. Mapping every swatch by raw RGB distance
// against the full enum lets ores, organs, springs and candles steal common
// building/creature colours. Classify the swatch into a material family first,
// then nearest-match only inside that family (plus a small shared fallback set).

typedef enum {
  MTX_STONE = 0,
  MTX_WOOD,
  MTX_FOLIAGE,
  MTX_GLASS,
  MTX_WOOL,
  MTX_THATCH,
  MTX_BRICK,
  MTX_METAL,
  MTX_FLESH,
  MTX_SOIL,
  MTX_WATER,
  MTX_FLAME,
  MTX_DARK,
  MTX_COUNT
} ModelMaterialFamily;

static int mtx_max3(int a, int b, int c)
{
  return a > b ? (a > c ? a : c) : (b > c ? b : c);
}
static int mtx_min3(int a, int b, int c)
{
  return a < b ? (a < c ? a : c) : (b < c ? b : c);
}

static ModelMaterialFamily mtx_classify_rgb(uint8_t r, uint8_t g, uint8_t b)
{
  const int maxc = mtx_max3(r, g, b);
  const int minc = mtx_min3(r, g, b);
  const int sat = maxc - minc;
  const int lum = (r * 299 + g * 587 + b * 114) / 1000;

  // Flame / ember (candle, campfire, magma accents)
  if (r > 170 && g > 40 && g < 200 && b < 90 && sat > 70 && lum > 60)
    return MTX_FLAME;

  // Water / deep blue
  if (b > r + 25 && b > g + 15 && b > 70 && sat > 30 && lum < 200)
    return MTX_WATER;

  // Glass: pale cyan / light blue / frosty white-blue (before wool whites)
  if (lum > 140 && b > r + 15 && b >= g && sat < 120)
    return MTX_GLASS;
  if (lum > 190 && sat < 40 && b > g + 5 && b > r + 5)
    return MTX_GLASS;

  // Bone ivory / cream (anatomical scaffolds) before wool steals pale whites.
  // Family MTX_FLESH includes VOXEL_BONE; nearest-match lands on bone colour.
  if (lum > 185 && sat < 55 && r >= 200 && g >= 195 && b >= 170 && b < 235)
  {
    const int rg = r > g ? r - g : g - r;
    if (rg < 30)
      return MTX_FLESH;
  }

  // Wool / cloth whites & light greys (not snow — that steals sheep/building whites)
  if (lum > 200 && sat < 45)
    return MTX_WOOL;
  if (lum > 165 && sat < 30)
    return MTX_WOOL;

  // Thatch / straw / sand-yellow / cream (before brick so yellow roofs aren't brick)
  if (r > 130 && g > 95 && b < 130 && r >= g - 10 && g > b + 15 && sat > 25 && lum > 100)
    return MTX_THATCH;

  // Foliage greens
  if (g > r + 18 && g > b + 18 && g > 55)
    return MTX_FOLIAGE;

  // Brick / terracotta reds (true reds only — not orange-yellow)
  if (r > 120 && r > g + 40 && r > b + 40 && g < 120 && lum < 180 && sat > 40)
    return MTX_BRICK;

  // Wood / leather browns
  if (r > 70 && r >= g && g >= b && sat > 22 && lum > 35 && lum < 195 && b < 140)
    return MTX_WOOD;

  // Flesh / pink / skin (include VOXEL_COLOR_FLESH 0xFFC8C8 = 255,200,200)
  if (r > 130 && g > 70 && g < 220 && b > 60 && b < 210 && r >= g - 5 && r > b + 10 &&
      sat > 20 && lum > 80 && lum < 240)
    return MTX_FLESH;

  // Dark almost-black
  if (lum < 42)
    return MTX_DARK;

  // Low-sat greys → stone by default; only bright cool greys go metal (steel/silver)
  if (sat < 28 && lum >= 42 && lum <= 200)
  {
    if (lum > 175 && b >= g && b >= r - 5)
      return MTX_METAL;
    return MTX_STONE;
  }

  // Dirt / soil browns
  if (lum < 130 && r > 55 && g > 35 && b < 90 && r >= g)
    return MTX_SOIL;

  return MTX_STONE;
}

static bool mtx_type_allowed_in_family(VoxelType t, ModelMaterialFamily fam)
{
  switch (fam)
  {
  case MTX_STONE:
    return t == VOXEL_STONE || t == VOXEL_STONE_BASALT || t == VOXEL_STONE_GRANITE ||
           t == VOXEL_STONE_LIMESTONE || t == VOXEL_STONE_SANDSTONE || t == VOXEL_COBBLE ||
           t == VOXEL_GRAVEL || t == VOXEL_PLASTER || t == VOXEL_ADOBE;
  case MTX_WOOD:
    return (t >= VOXEL_WOOD && t <= VOXEL_WOOD_REDWOOD) || t == VOXEL_PLANK ||
           t == VOXEL_LEATHER || t == VOXEL_FUR || t == VOXEL_HORN;
  case MTX_FOLIAGE:
    return (t >= VOXEL_LEAVES && t <= VOXEL_LEAVES_REDWOOD) ||
           (t >= VOXEL_GRASS && t <= VOXEL_GRASS_MOSS) || t == VOXEL_GRASS_TALL ||
           (t >= VOXEL_BUSH && t <= VOXEL_BUSH_STRAWBERRY);
  case MTX_GLASS:
    return t == VOXEL_GLASS || (t >= VOXEL_GLASS_WHITE && t <= VOXEL_GLASS_YELLOW) ||
           t == VOXEL_ICE;
  case MTX_WOOL:
    // Prefer dyed/generic wool + feather/cloth/plaster. No snow/bone (they steal whites).
    return t == VOXEL_WOOL || (t >= VOXEL_WOOL_WHITE && t <= VOXEL_WOOL_YELLOW) ||
           t == VOXEL_CLOTH || t == VOXEL_FEATHER || t == VOXEL_PLASTER || t == VOXEL_PAPER;
  case MTX_THATCH:
    return t == VOXEL_THATCH || t == VOXEL_STRAW || t == VOXEL_SAND ||
           t == VOXEL_SAND_SANDSTONE || t == VOXEL_WAX || t == VOXEL_WOOL_YELLOW ||
           t == VOXEL_STONE_SANDSTONE || t == VOXEL_FEATHER;
  case MTX_BRICK:
    return t == VOXEL_BRICK || t == VOXEL_TERRACOTTA || t == VOXEL_CLAY ||
           t == VOXEL_CERAMIC || t == VOXEL_ADOBE || t == VOXEL_WOOL_RED;
  case MTX_METAL:
    // No aluminum (near-white) — it steals wool/plaster. No titanium-ish mid greys over stone.
    return t == VOXEL_STEEL || t == VOXEL_IRON || t == VOXEL_COPPER || t == VOXEL_TIN ||
           t == VOXEL_SILVER || t == VOXEL_GOLD || t == VOXEL_LEAD || t == VOXEL_ZINC ||
           t == VOXEL_NICKEL || t == VOXEL_COBALT || t == VOXEL_PLATINUM;
  case MTX_FLESH:
    return t == VOXEL_FLESH || t == VOXEL_BLOOD || t == VOXEL_BONE || t == VOXEL_LEATHER;
  case MTX_SOIL:
    return t == VOXEL_SOIL || t == VOXEL_SOIL_CLAY || t == VOXEL_SOIL_LOAM ||
           t == VOXEL_SOIL_SILT || t == VOXEL_ADOBE || t == VOXEL_CLAY || t == VOXEL_ASH;
  case MTX_WATER:
    return t == VOXEL_WATER || t == VOXEL_ICE || t == VOXEL_GLASS_BLUE;
  case MTX_FLAME:
    return t == VOXEL_CAMPFIRE || t == VOXEL_MAGMA || t == VOXEL_GOLD || t == VOXEL_TERRACOTTA;
  case MTX_DARK:
    return t == VOXEL_STONE_BASALT || t == VOXEL_OBSIDIAN || t == VOXEL_WOOL_BLACK ||
           t == VOXEL_RUBBER || t == VOXEL_IRON || t == VOXEL_ASH;
  default:
    return false;
  }
}

static bool mtx_type_never_from_palette(VoxelType t)
{
  // Ores / springs / gases / organs / actor / air must not win arbitrary swatches.
  if (t == VOXEL_AIR || t == VOXEL_ACTOR || t == VOXEL_WORLD)
    return true;
  if (t >= VOXEL_ORE && t <= VOXEL_ORE_PLATINUM)
    return true;
  if (t >= VOXEL_SPRING && t <= VOXEL_SPRING_GAS)
    return true;
  if (t == VOXEL_STEAM || t == VOXEL_GAS || t == VOXEL_OIL)
    return true;
  if (t == VOXEL_ORGAN || t == VOXEL_BRAIN || t == VOXEL_CANDLE || t == VOXEL_ALUMINUM ||
      t == VOXEL_LIMESTONE || t == VOXEL_BEDROCK)
    return true;
  return false;
}

// Prefer gameplay-canonical materials when distances are close.
static int mtx_type_preference_penalty(VoxelType t, ModelMaterialFamily fam)
{
  switch (fam)
  {
  case MTX_WOOL:
    if (t == VOXEL_WOOL || t == VOXEL_WOOL_WHITE) return 0;
    if (t >= VOXEL_WOOL_BLACK && t <= VOXEL_WOOL_YELLOW) return 80;
    if (t == VOXEL_FEATHER) return 200;
    if (t == VOXEL_PLASTER) return 400;
    return 600;
  case MTX_STONE:
    if (t == VOXEL_STONE || t == VOXEL_COBBLE || t == VOXEL_STONE_LIMESTONE) return 0;
    if (t == VOXEL_PLASTER) return 150;
    if (t == VOXEL_STONE_SANDSTONE || t == VOXEL_ADOBE) return 200;
    return 300;
  case MTX_WOOD:
    if (t == VOXEL_WOOD || t == VOXEL_PLANK) return 0;
    return 200;
  case MTX_THATCH:
    if (t == VOXEL_THATCH || t == VOXEL_STRAW) return 0;
    if (t == VOXEL_WOOL_YELLOW) return 100;
    return 250;
  case MTX_GLASS:
    if (t == VOXEL_GLASS || t == VOXEL_GLASS_WHITE) return 0;
    return 150;
  case MTX_BRICK:
    if (t == VOXEL_BRICK || t == VOXEL_TERRACOTTA) return 0;
    return 200;
  case MTX_METAL:
    if (t == VOXEL_STEEL || t == VOXEL_IRON) return 0;
    return 200;
  case MTX_DARK:
    if (t == VOXEL_OBSIDIAN || t == VOXEL_WOOL_BLACK || t == VOXEL_STONE_BASALT) return 0;
    return 200;
  case MTX_FLESH:
    if (t == VOXEL_FLESH) return 0;
    if (t == VOXEL_BONE) return 40;
    if (t == VOXEL_BLOOD || t == VOXEL_LEATHER) return 80;
    return 200;
  default:
    return 0;
  }
}

static VoxelType model_transformer_map_palette_rgb(uint8_t r, uint8_t g, uint8_t b,
                                                    VoxelType fallback_type)
{
  const ModelMaterialFamily fam = mtx_classify_rgb(r, g, b);
  int best_d = 1 << 30;
  VoxelType best_t = fallback_type;
  int matched_in_family = 0;

  for (int pass = 0; pass < 2; pass++)
  {
    best_d = 1 << 30;
    best_t = fallback_type;
    matched_in_family = 0;
    for (int ti = 0; ti < (int)VOXEL_COUNT; ti++)
    {
      const VoxelType t = (VoxelType)ti;
      if (mtx_type_never_from_palette(t))
        continue;
      if (pass == 0 && !mtx_type_allowed_in_family(t, fam))
        continue;
      uint8_t tr, tg, tb;
      world_voxel_type_color(t, &tr, &tg, &tb);
      const int dr = (int)r - (int)tr;
      const int dg = (int)g - (int)tg;
      const int db = (int)b - (int)tb;
      int d = dr * dr + dg * dg + db * db;
      if (pass == 0)
        d += mtx_type_preference_penalty(t, fam);
      if (d < best_d)
      {
        best_d = d;
        best_t = t;
        matched_in_family = 1;
      }
    }
    if (matched_in_family)
      break;
  }
  return best_t;
}

// Read MagicaVoxel .vox and stamp into world starting at origin
bool model_transformer_load_vox(World* world, const char* vox_filepath,
                                uint32_t origin_x, uint32_t origin_y, uint32_t origin_z,
                                VoxelType fallback_type) {
  if (!world || !vox_filepath) return false;

  FILE* f = fopen(vox_filepath, "rb");
  if (!f) return false;

  char magic[4];
  if (fread(magic, 1, 4, f) != 4 || strncmp(magic, "VOX ", 4) != 0) {
    fclose(f);
    return false;
  }
  uint32_t version = read_u32_le(f);
  (void)version; // we accept any

  // MAIN chunk header
  char chunk_id[4];
  if (fread(chunk_id, 1, 4, f) != 4 || strncmp(chunk_id, "MAIN", 4) != 0) {
    fclose(f);
    return false;
  }
  uint32_t main_content = read_u32_le(f);
  uint32_t main_children = read_u32_le(f);
  (void)main_content; (void)main_children;

  uint32_t size_x = 0, size_y = 0, size_z = 0;
  // Optional palette from RGBA chunk (MagicaVoxel). If absent, we'll fallback.
  bool have_palette = false;
  uint8_t palette[256][4];
  // Collect XYZI entries so we can map after reading the palette
  typedef struct { uint8_t x, y, z, c; } XYZIEntry;
  size_t entries_count = 0, entries_capacity = 0;
  XYZIEntry* entries = NULL;

  // Very minimal loop through chunks to find SIZE and XYZI
  while (!feof(f)) {
    if (fread(chunk_id, 1, 4, f) != 4) break;
    uint32_t content_size = read_u32_le(f);
    uint32_t children_size = read_u32_le(f);
    long chunk_start = ftell(f);

    if (strncmp(chunk_id, "SIZE", 4) == 0) {
      size_x = read_u32_le(f);
      size_y = read_u32_le(f);
      size_z = read_u32_le(f);
    } else if (strncmp(chunk_id, "XYZI", 4) == 0) {
      uint32_t num_voxels = read_u32_le(f);
      // Ensure capacity
      if (entries_count + num_voxels > entries_capacity) {
        size_t new_cap = entries_capacity ? entries_capacity : 1024;
        while (new_cap < entries_count + num_voxels) new_cap *= 2;
        XYZIEntry* n = (XYZIEntry*)realloc(entries, new_cap * sizeof(XYZIEntry));
        if (!n) { fclose(f); free(entries); return false; }
        entries = n; entries_capacity = new_cap;
      }
      for (uint32_t i = 0; i < num_voxels; i++) {
        entries[entries_count].x = read_u8(f);
        entries[entries_count].y = read_u8(f);
        entries[entries_count].z = read_u8(f);
        entries[entries_count].c = read_u8(f);
        entries_count++;
      }
    } else if (strncmp(chunk_id, "RGBA", 4) == 0) {
      // Read 256 RGBA entries
      for (int i = 0; i < 256; i++) {
        uint8_t r = read_u8(f);
        uint8_t g = read_u8(f);
        uint8_t b = read_u8(f);
        uint8_t a = read_u8(f);
        palette[i][0] = r; palette[i][1] = g; palette[i][2] = b; palette[i][3] = a;
      }
      have_palette = true;
    } else {
      // ignore other chunks (RGBA, nTRN, nGRP, etc.) for minimal import
    }

    // Skip to next chunk safely
    fseek(f, chunk_start + content_size, SEEK_SET);
    // Move past children block content as well
    fseek(f, children_size, SEEK_CUR);
  }

  fclose(f);

  // Palette → gameplay material. Pure nearest-RGB against every VoxelType lets ores, organs,
  // springs and candles steal building/creature colours (castles became candle; sheep became
  // aluminium ore). Classify the swatch first, then nearest-match inside a curated family.
  VoxelType color_to_voxel[256];
  for (int i = 0; i < 256; i++)
    color_to_voxel[i] = fallback_type;
  if (have_palette)
  {
    for (int ci = 1; ci <= 255; ci++)
    {
      uint8_t r = palette[ci - 1][0];
      uint8_t g = palette[ci - 1][1];
      uint8_t b = palette[ci - 1][2];
      uint8_t a = palette[ci - 1][3];
      if (a < 16)
      {
        color_to_voxel[ci] = VOXEL_AIR;
        continue;
      }
      color_to_voxel[ci] = model_transformer_map_palette_rgb(r, g, b, fallback_type);
    }
  }

  // Stamp collected voxels
  for (size_t i = 0; i < entries_count; i++) {
    uint32_t wx = origin_x + entries[i].x;
    uint32_t wy = origin_y + entries[i].y;
    uint32_t wz = origin_z + entries[i].z;
    if (!world_is_position_valid(world, (int)wx, (int)wy, (int)wz)) continue;
    VoxelType t = fallback_type;
    if (have_palette) {
      uint8_t ci = entries[i].c;
      if (ci == 0) t = VOXEL_AIR; else t = color_to_voxel[ci];
    }
    world_set_voxel(world, wx, wy, wz, t);
  }
  free(entries);
  return true;
}

// Minimal BMP writing helper
static bool write_bmp_rgb24(const char* filepath, int width, int height, const uint8_t* rgb_pixels) {
  // BMP header sizes
  const int file_header_size = 14;
  const int info_header_size = 40;
  const int row_stride = ((width * 3 + 3) / 4) * 4; // rows padded to 4 bytes
  const int pixel_array_size = row_stride * height;
  const int file_size = file_header_size + info_header_size + pixel_array_size;

  FILE* f = fopen(filepath, "wb");
  if (!f) return false;

  // BITMAPFILEHEADER
  uint8_t file_header[14] = {
    'B','M',                      // Signature
    (uint8_t)(file_size & 0xFF), (uint8_t)((file_size >> 8) & 0xFF), (uint8_t)((file_size >> 16) & 0xFF), (uint8_t)((file_size >> 24) & 0xFF),
    0,0, 0,0,                     // Reserved
    (uint8_t)((file_header_size + info_header_size) & 0xFF),
    (uint8_t)(((file_header_size + info_header_size) >> 8) & 0xFF),
    (uint8_t)(((file_header_size + info_header_size) >> 16) & 0xFF),
    (uint8_t)(((file_header_size + info_header_size) >> 24) & 0xFF)
  };
  fwrite(file_header, 1, sizeof(file_header), f);

  // BITMAPINFOHEADER (DIB)
  uint8_t info_header[40] = {0};
  info_header[0] = 40; // header size
  // width, height (little endian)
  info_header[4] = (uint8_t)(width & 0xFF);
  info_header[5] = (uint8_t)((width >> 8) & 0xFF);
  info_header[6] = (uint8_t)((width >> 16) & 0xFF);
  info_header[7] = (uint8_t)((width >> 24) & 0xFF);
  info_header[8] = (uint8_t)(height & 0xFF);
  info_header[9] = (uint8_t)((height >> 8) & 0xFF);
  info_header[10]= (uint8_t)((height >> 16) & 0xFF);
  info_header[11]= (uint8_t)((height >> 24) & 0xFF);
  info_header[12]= 1; // planes
  info_header[14]= 24; // bpp
  // image size
  info_header[20]= (uint8_t)(pixel_array_size & 0xFF);
  info_header[21]= (uint8_t)((pixel_array_size >> 8) & 0xFF);
  info_header[22]= (uint8_t)((pixel_array_size >> 16) & 0xFF);
  info_header[23]= (uint8_t)((pixel_array_size >> 24) & 0xFF);
  fwrite(info_header, 1, sizeof(info_header), f);

  // Pixels are stored bottom-up in BMP
  uint8_t* row = (uint8_t*)malloc(row_stride);
  if (!row) { fclose(f); return false; }
  for (int y = 0; y < height; y++) {
    int src_y = height - 1 - y; // bottom-up
    const uint8_t* src = rgb_pixels + src_y * width * 3;
    // Convert RGB to BGR and pad
    int pos = 0;
    for (int x = 0; x < width; x++) {
      uint8_t r = src[x*3 + 0];
      uint8_t g = src[x*3 + 1];
      uint8_t b = src[x*3 + 2];
      row[pos++] = b; row[pos++] = g; row[pos++] = r;
    }
    // pad
    while (pos < row_stride) row[pos++] = 0;
    fwrite(row, 1, row_stride, f);
  }
  free(row);
  fclose(f);
  return true;
}

// Minimal BMP reader supporting 24-bit BGR and 32-bit BGRA (uncompressed BI_RGB)
static bool read_bmp_rgb(const char* filepath, int* out_w, int* out_h, uint8_t** out_rgb, uint8_t** out_a) {
  FILE* f = fopen(filepath, "rb");
  if (!f) return false;

  uint8_t header[14];
  if (fread(header, 1, 14, f) != 14) { fclose(f); return false; }
  if (header[0] != 'B' || header[1] != 'M') { fclose(f); return false; }

  uint8_t dib[40];
  if (fread(dib, 1, 40, f) != 40) { fclose(f); return false; }
  uint32_t dib_size = dib[0] | (dib[1]<<8) | (dib[2]<<16) | (dib[3]<<24);
  if (dib_size < 40) { fclose(f); return false; }

  int32_t width = (int32_t)(dib[4] | (dib[5]<<8) | (dib[6]<<16) | (dib[7]<<24));
  int32_t height = (int32_t)(dib[8] | (dib[9]<<8) | (dib[10]<<16) | (dib[11]<<24));
  uint16_t planes = (uint16_t)(dib[12] | (dib[13]<<8));
  uint16_t bpp = (uint16_t)(dib[14] | (dib[15]<<8));
  uint32_t compression = (uint32_t)(dib[16] | (dib[17]<<8) | (dib[18]<<16) | (dib[19]<<24));
  if (planes != 1 || (bpp != 24 && bpp != 32) || compression != 0) { fclose(f); return false; }

  // Pixel array offset
  uint32_t offset = header[10] | (header[11]<<8) | (header[12]<<16) | (header[13]<<24);
  fseek(f, offset, SEEK_SET);

  int w = width;
  int h = height < 0 ? -height : height; // handle top-down BMP when height negative
  int row_stride_bmp;
  bool has_alpha = (bpp == 32);
  if (bpp == 24) row_stride_bmp = ((w * 3 + 3) / 4) * 4; else row_stride_bmp = w * 4;

  uint8_t* rgb = (uint8_t*)malloc(w * h * 3);
  uint8_t* alpha = has_alpha ? (uint8_t*)malloc(w * h) : NULL;
  if (!rgb || (has_alpha && !alpha)) { free(rgb); free(alpha); fclose(f); return false; }

  for (int y = 0; y < h; y++) {
    int dst_y = (height > 0) ? (h - 1 - y) : y; // if positive height, BMP is bottom-up
    uint8_t* rowbuf = (uint8_t*)malloc(row_stride_bmp);
    if (!rowbuf) { free(rgb); free(alpha); fclose(f); return false; }
    if (fread(rowbuf, 1, row_stride_bmp, f) != (size_t)row_stride_bmp) { free(rowbuf); free(rgb); free(alpha); fclose(f); return false; }
    for (int x = 0; x < w; x++) {
      uint8_t b = rowbuf[x*(bpp/8) + 0];
      uint8_t g = rowbuf[x*(bpp/8) + 1];
      uint8_t r = rowbuf[x*(bpp/8) + 2];
      uint8_t a = (bpp == 32) ? rowbuf[x*4 + 3] : 255;
      int idx = (dst_y * w + x);
      rgb[idx*3 + 0] = r;
      rgb[idx*3 + 1] = g;
      rgb[idx*3 + 2] = b;
      if (has_alpha) alpha[idx] = a;
    }
    free(rowbuf);
  }

  fclose(f);
  *out_w = w; *out_h = h; *out_rgb = rgb; *out_a = alpha;
  return true;
}

// Load an image (BMP) and stamp as voxels on plane Z
bool model_transformer_load_image_plane(World* world, const char* image_filepath,
                                        uint32_t plane_z, bool transparent_is_air) {
  if (!world || !image_filepath) return false;

  int w = 0, h = 0; uint8_t* rgb = NULL; uint8_t* a = NULL;
  if (!read_bmp_rgb(image_filepath, &w, &h, &rgb, &a)) {
    return false;
  }

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint8_t r = rgb[(y*w + x)*3 + 0];
      uint8_t g = rgb[(y*w + x)*3 + 1];
      uint8_t b = rgb[(y*w + x)*3 + 2];
      uint8_t alpha = a ? a[y*w + x] : 255;

      uint32_t wx = (uint32_t)x;
      uint32_t wy = (uint32_t)y;
      if (!world_is_position_valid(world, wx, wy, plane_z)) continue;
      if (transparent_is_air && alpha < 128) {
        world_set_voxel(world, wx, wy, plane_z, VOXEL_AIR);
      } else {
        VoxelType t = map_rgb_to_voxel_type(r, g, b);
        world_set_voxel(world, wx, wy, plane_z, t);
      }
    }
  }

  free(rgb); if (a) free(a);
  return true;
}

// Export world to a minimal .vox file (single model)
bool model_transformer_export_vox(const World* world, const char* vox_filepath) {
  if (!world || !vox_filepath) return false;
  FILE* f = fopen(vox_filepath, "wb");
  if (!f) return false;

  // Collect all solid voxels into a buffer
  typedef struct { uint8_t x, y, z, c; } V;
  size_t capacity = 1024;
  size_t count = 0;
  V* vox = (V*)malloc(capacity * sizeof(V));
  if (!vox) { fclose(f); return false; }

  for (uint32_t z = 0; z < world->depth; z++) {
    for (uint32_t y = 0; y < world->height; y++) {
      for (uint32_t x = 0; x < world->width; x++) {
        const Voxel* v = world_get_voxel((World*)world, x, y, z);
        if (!v) continue;
        if (v->type == VOXEL_AIR) continue;
        if (count == capacity) {
          capacity *= 2; V* n = (V*)realloc(vox, capacity * sizeof(V));
          if (!n) { free(vox); fclose(f); return false; }
          vox = n;
        }
        uint8_t r,g,b; voxel_type_to_rgb(v->type, &r,&g,&b);
        // very small palette index: bucketize
        uint8_t cindex = (uint8_t)(((uint32_t)r + g + b) % 255 + 1);
        vox[count++] = (V){ (uint8_t)x, (uint8_t)y, (uint8_t)z, cindex };
      }
    }
  }

  // Write header
  fwrite("VOX ", 1, 4, f);
  write_u32_le(f, 150); // version

  // We'll compute chunk sizes:
  // SIZE content: 12 bytes (x,y,z)
  // XYZI content: 4 + N*4
  // MAIN children size = SIZE chunk (12+12) + XYZI chunk (4+N*4+12)
  uint32_t size_content = 12;
  uint32_t xyzi_content = 4 + (uint32_t)count * 4;
  uint32_t size_chunk = 12 + size_content; // id+content+children
  uint32_t xyzi_chunk = 12 + xyzi_content;
  uint32_t main_children = size_chunk + xyzi_chunk;

  // MAIN chunk
  fwrite("MAIN", 1, 4, f);
  write_u32_le(f, 0); // content bytes
  write_u32_le(f, main_children); // children bytes

  // SIZE chunk
  fwrite("SIZE", 1, 4, f);
  write_u32_le(f, size_content);
  write_u32_le(f, 0); // no children
  write_u32_le(f, world->width);
  write_u32_le(f, world->height);
  write_u32_le(f, world->depth);

  // XYZI chunk
  fwrite("XYZI", 1, 4, f);
  write_u32_le(f, xyzi_content);
  write_u32_le(f, 0);
  write_u32_le(f, (uint32_t)count);
  for (size_t i = 0; i < count; i++) {
    write_u8(f, vox[i].x);
    write_u8(f, vox[i].y);
    write_u8(f, vox[i].z);
    write_u8(f, vox[i].c);
  }

  free(vox);
  fclose(f);
  return true;
}

// Export simple image from a camera using top-down projection
bool model_transformer_export_image(const World* world, const char* image_filepath,
                                    int camera_x, int camera_y, int camera_z,
                                    const char* projection) {
  (void)camera_x; (void)camera_y; (void)camera_z; // not used in topdown
  if (!world || !image_filepath || !projection) return false;

  if (strcmp(projection, "topdown") != 0) {
    // only support topdown for now
    return false;
  }

  const int width = (int)world->width;
  const int height = (int)world->height;
  uint8_t* rgb = (uint8_t*)malloc(width * height * 3);
  if (!rgb) return false;

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      // find highest solid voxel at (x,y)
      VoxelType t = VOXEL_AIR;
      for (int z = (int)world->depth - 1; z >= 0; z--) {
        const Voxel* v = world_get_voxel((World*)world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (!v) continue;
        if (v->type != VOXEL_AIR) { t = v->type; break; }
      }
      uint8_t r,g,b; voxel_type_to_rgb(t, &r,&g,&b);
      rgb[(y*width + x)*3 + 0] = r;
      rgb[(y*width + x)*3 + 1] = g;
      rgb[(y*width + x)*3 + 2] = b;
    }
  }

  bool ok = write_bmp_rgb24(image_filepath, width, height, rgb);
  free(rgb);
  return ok;
}


// --- Minimal Minecraft .schematic (gzipped NBT) importer --------------------
// We avoid linking zlib by invoking system gunzip -c via popen for .gz files.
// Supports the classic MCEdit schematic fields: Width, Height, Length, Blocks.
// Reference: https://minecraft.fandom.com/wiki/Schematic_file_format

typedef struct {
  FILE* fp;
  bool from_pipe;
} NBTStream;

static NBTStream nbt_open(const char* path) {
  NBTStream s = {0};
  size_t len = strlen(path);
  if (len >= 3 && strcmp(path + len - 3, ".gz") == 0) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "gunzip -c '%s'", path);
    FILE* p = popen(cmd, "r");
    s.fp = p; s.from_pipe = true;
  } else {
    s.fp = fopen(path, "rb"); s.from_pipe = false;
  }
  return s;
}

static void nbt_close(NBTStream* s) {
  if (!s || !s->fp) return;
  if (s->from_pipe) pclose(s->fp); else fclose(s->fp);
  s->fp = NULL;
}

// NBT basic readers (big endian)
static uint16_t nbt_read_u16_be(FILE* f) {
  int b0 = fgetc(f); int b1 = fgetc(f);
  if (b0 == EOF || b1 == EOF) return 0;
  return (uint16_t)((b0 << 8) | b1);
}
static uint32_t nbt_read_u32_be(FILE* f) {
  int b0 = fgetc(f); int b1 = fgetc(f); int b2 = fgetc(f); int b3 = fgetc(f);
  if (b0 == EOF || b1 == EOF || b2 == EOF || b3 == EOF) return 0;
  return ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | (uint32_t)b3;
}

// Read a length-prefixed UTF-8 string
static bool nbt_read_string(FILE* f, char* out, size_t out_sz) {
  uint16_t n = nbt_read_u16_be(f);
  if (n == 0) { if (out && out_sz) out[0] = '\0'; return true; }
  if (!out || out_sz == 0) { fseek(f, n, SEEK_CUR); return true; }
  if ((size_t)n >= out_sz) {
    // read but truncate
    size_t i;
    for (i = 0; i < out_sz - 1 && i < n; i++) out[i] = (char)fgetc(f);
    for (; i < n; i++) (void)fgetc(f);
    out[out_sz - 1] = '\0';
    return true;
  }
  for (uint16_t i = 0; i < n; i++) out[i] = (char)fgetc(f);
  out[n] = '\0';
  return true;
}

// NBT tags
enum { TAG_End=0, TAG_Byte=1, TAG_Short=2, TAG_Int=3, TAG_Long=4, TAG_Float=5,
       TAG_Double=6, TAG_Byte_Array=7, TAG_String=8, TAG_List=9, TAG_Compound=10,
       TAG_Int_Array=11, TAG_Long_Array=12 };

// Minimal parser to reach root compound and locate needed fields
typedef struct {
  uint16_t width, height, length;
  uint8_t* blocks; // size width*height*length
  size_t blocks_len;
} Schematic;

static void schematic_init(Schematic* s) { memset(s, 0, sizeof(*s)); }
static void schematic_free(Schematic* s) { if (s && s->blocks) { free(s->blocks); s->blocks = NULL; } }

static bool nbt_skip_payload(FILE* f, int tag_type);

static bool nbt_read_named_tag(FILE* f, int* out_type, char* name, size_t name_sz) {
  int t = fgetc(f);
  if (t == EOF) return false;
  *out_type = t;
  if (t == TAG_End) { if (name && name_sz) name[0] = '\0'; return true; }
  return nbt_read_string(f, name, name_sz);
}

static bool nbt_read_payload_into(FILE* f, int type, Schematic* out) {
  switch (type) {
    case TAG_Short: (void)nbt_read_u16_be(f); return true;
    case TAG_Int:   (void)nbt_read_u32_be(f); return true;
    case TAG_String: {
      char tmp[64]; return nbt_read_string(f, tmp, sizeof(tmp));
    }
    case TAG_Byte_Array: {
      uint32_t len = nbt_read_u32_be(f);
      // Only capture Blocks if not already set
      if (out && out->blocks == NULL) {
        out->blocks = (uint8_t*)malloc(len);
        if (!out->blocks) { fseek(f, len, SEEK_CUR); return false; }
        out->blocks_len = len;
        if (fread(out->blocks, 1, len, f) != len) return false;
      } else {
        fseek(f, len, SEEK_CUR);
      }
      return true;
    }
    case TAG_List: {
      int elem_type = fgetc(f);
      uint32_t len = nbt_read_u32_be(f);
      for (uint32_t i = 0; i < len; i++) {
        if (elem_type == TAG_Compound) {
          // recursively skip compounds
          while (1) {
            int ct; char nm[64]; if (!nbt_read_named_tag(f, &ct, nm, sizeof(nm))) return false;
            if (ct == TAG_End) break;
            if (!nbt_skip_payload(f, ct)) return false;
          }
        } else {
          if (!nbt_skip_payload(f, elem_type)) return false;
        }
      }
      return true;
    }
    case TAG_Compound: {
      while (1) {
        int t; char nm[64]; if (!nbt_read_named_tag(f, &t, nm, sizeof(nm))) return false;
        if (t == TAG_End) break;
        // Capture needed fields by name
        if (strcmp(nm, "Width") == 0 && t == TAG_Short) {
          out->width = nbt_read_u16_be(f);
        } else if (strcmp(nm, "Height") == 0 && t == TAG_Short) {
          out->height = nbt_read_u16_be(f);
        } else if (strcmp(nm, "Length") == 0 && t == TAG_Short) {
          out->length = nbt_read_u16_be(f);
        } else if (strcmp(nm, "Blocks") == 0 && t == TAG_Byte_Array) {
          // read into blocks
          if (!nbt_read_payload_into(f, t, out)) return false;
        } else {
          if (!nbt_skip_payload(f, t)) return false;
        }
      }
      return true;
    }
    default: break;
  }
  // TAG_Byte, TAG_Long, etc: skip minimal
  if (type == TAG_Byte) { (void)fgetc(f); return true; }
  if (type == TAG_Long) { for (int i=0;i<8;i++) (void)fgetc(f); return true; }
  if (type == TAG_Float) { for (int i=0;i<4;i++) (void)fgetc(f); return true; }
  if (type == TAG_Double) { for (int i=0;i<8;i++) (void)fgetc(f); return true; }
  if (type == TAG_Int_Array) { uint32_t n=nbt_read_u32_be(f); fseek(f, (long)(n*4ULL), SEEK_CUR); return true; }
  if (type == TAG_Long_Array) { uint32_t n=nbt_read_u32_be(f); fseek(f, (long)(n*8ULL), SEEK_CUR); return true; }
  return false;
}

static bool nbt_skip_payload(FILE* f, int tag_type) { return nbt_read_payload_into(f, tag_type, NULL); }

bool model_transformer_probe_schematic_dimensions(
    const char* schematic_filepath,
    uint16_t* out_width,
    uint16_t* out_height,
    uint16_t* out_length) {
  if (!schematic_filepath || !out_width || !out_height || !out_length) return false;
  NBTStream s = nbt_open(schematic_filepath);
  if (!s.fp) return false;
  int tag = fgetc(s.fp);
  if (tag != TAG_Compound) { nbt_close(&s); return false; }
  char root_name[64]; nbt_read_string(s.fp, root_name, sizeof(root_name));
  // Walk compound until we read Width/Height/Length
  uint16_t w=0,h=0,l=0;
  while (1) {
    int t; char nm[64]; if (!nbt_read_named_tag(s.fp, &t, nm, sizeof(nm))) { nbt_close(&s); return false; }
    if (t == TAG_End) break;
    if (strcmp(nm, "Width") == 0 && t == TAG_Short) {
      w = nbt_read_u16_be(s.fp);
    } else if (strcmp(nm, "Height") == 0 && t == TAG_Short) {
      h = nbt_read_u16_be(s.fp);
    } else if (strcmp(nm, "Length") == 0 && t == TAG_Short) {
      l = nbt_read_u16_be(s.fp);
    } else {
      if (!nbt_skip_payload(s.fp, t)) { nbt_close(&s); return false; }
    }
  }
  nbt_close(&s);
  if (w == 0 || h == 0 || l == 0) return false;
  *out_width = w; *out_height = h; *out_length = l;
  return true;
}

// Map Minecraft block id to VoxelType (classic subset)
static VoxelType mc_block_to_voxel(uint8_t id) {
  switch (id) {
    case 0: return VOXEL_AIR; // Air
    case 1: return VOXEL_STONE; // Stone
    case 2: return VOXEL_SOIL; // Grass block (surface)
    case 3: return VOXEL_SOIL; // Soil
    case 8: // Water (flowing)
    case 9: return VOXEL_WATER; // Water (still)
    case 12: return VOXEL_SAND; // Sand
    case 13: return VOXEL_STONE_GRANITE; // Gravel → coarse stone
    case 14: return VOXEL_ORE_GOLD; // Gold ore
    case 15: return VOXEL_ORE_IRON; // Iron ore
    case 16: return VOXEL_ORE_COAL; // Coal ore
    case 17: return VOXEL_WOOD; // Log
    case 18: return VOXEL_LEAVES; // Leaves
    case 20: return VOXEL_GLASS; // Glass
    case 24: return VOXEL_STONE_SANDSTONE; // Sandstone (using limestone)
    case 35: return VOXEL_WOOL; // Wool (generic)
    case 41: return VOXEL_GOLD; // Gold block
    case 42: return VOXEL_ORE_IRON; // Iron block
    case 45: return VOXEL_BRICK; // Bricks
    case 49: return VOXEL_OBSIDIAN; // Obsidian
    case 73: return VOXEL_ORE_HEMATITE; // Redstone ore
    case 79: return VOXEL_ICE; // Ice
    case 80: return VOXEL_SNOW; // Snow block
    case 82: return VOXEL_CLAY; // Clay block
    case 22: return VOXEL_CRYSTAL_BLUE; // Lapis block
    case 21: return VOXEL_CRYSTAL_BLUE; // Lapis ore
    case 56: return VOXEL_CRYSTAL; // Diamond ore
    case 57: return VOXEL_CRYSTAL; // Diamond block
    case 87: return VOXEL_STONE_BASALT; // Netherrack
    case 88: return VOXEL_SAND; // Soul sand
    case 89: return VOXEL_CRYSTAL; // Glowstone → crystal
    default: return VOXEL_STONE;
  }
}

bool model_transformer_import_minecraft_schematic(
    World* world,
    const char* schematic_filepath,
    uint32_t origin_x,
    uint32_t origin_y,
    uint32_t origin_z) {
  if (!world || !schematic_filepath) return false;
  NBTStream s = nbt_open(schematic_filepath);
  if (!s.fp) return false;

  // Root: compound with name (skip name)
  int tag = fgetc(s.fp);
  if (tag != TAG_Compound) { nbt_close(&s); return false; }
  char root_name[64]; nbt_read_string(s.fp, root_name, sizeof(root_name));

  Schematic sc; schematic_init(&sc);
  bool ok = nbt_read_payload_into(s.fp, TAG_Compound, &sc);
  if (!ok || sc.width == 0 || sc.height == 0 || sc.length == 0 || !sc.blocks) {
    schematic_free(&sc); nbt_close(&s); return false;
  }

  // Blocks is indexed Y-major according to spec: index = (y * Length + z) * Width + x
  size_t expected = (size_t)sc.width * sc.height * sc.length;
  if (sc.blocks_len < expected) { schematic_free(&sc); nbt_close(&s); return false; }

  for (uint16_t y = 0; y < sc.height; y++) {
    for (uint16_t z = 0; z < sc.length; z++) {
      for (uint16_t x = 0; x < sc.width; x++) {
        size_t idx = ((size_t)y * sc.length + z) * sc.width + x;
        uint8_t id = sc.blocks[idx];
        VoxelType vt = mc_block_to_voxel(id);
        uint32_t wx = origin_x + x;
        uint32_t wy = origin_y + z; // schematic Z maps to our Y
        uint32_t wz = origin_z + y; // schematic Y maps to our Z (height)
        if (!world_is_position_valid(world, (int)wx, (int)wy, (int)wz)) continue;
        world_set_voxel(world, wx, wy, wz, vt);
      }
    }
  }

  schematic_free(&sc);
  nbt_close(&s);
  return true;
}


