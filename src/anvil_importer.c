// anvil_importer.c
// Minimal importer for a Minecraft Anvil world folder: loads the spawn chunk
// from region/*.mca and saves as a .world in worlds/.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdbool.h>
#include <zlib.h>

#include "world.h"

// Reuse minimal NBT helpers from model_transformer.c by forward-declaring
// the subset we need and including the file as a compilation unit is messy.
// Implement tiny in-memory NBT reader here for required tags.

enum { TAG_End=0, TAG_Byte=1, TAG_Short=2, TAG_Int=3, TAG_Long=4, TAG_Float=5,
       TAG_Double=6, TAG_Byte_Array=7, TAG_String=8, TAG_List=9, TAG_Compound=10,
       TAG_Int_Array=11, TAG_Long_Array=12 };

typedef struct { const uint8_t* p; size_t n; size_t pos; } MBuf;
static bool mb_read(const MBuf* b, size_t n) { return b->pos + n <= b->n; }
static uint8_t mb_u8(MBuf* b){ return (mb_read(b,1)? b->p[b->pos++]:0); }
static uint16_t mb_u16be(MBuf* b){ uint16_t v=0; v = (uint16_t)(mb_u8(b)<<8); v |= mb_u8(b); return v; }
static uint32_t mb_u32be(MBuf* b){ uint32_t v=0; v = (uint32_t)mb_u8(b)<<24; v |= (uint32_t)mb_u8(b)<<16; v |= (uint32_t)mb_u8(b)<<8; v |= (uint32_t)mb_u8(b); return v; }
static uint64_t mb_u64be(MBuf* b){ uint64_t v=0; for(int i=0;i<8;i++){ v = (v<<8) | mb_u8(b); } return v; }
static void mb_skip(MBuf* b, size_t n){ if (mb_read(b,n)) b->pos += n; else b->pos = b->n; }
static void mb_str(MBuf* b, char* out, size_t out_sz){ uint16_t len=mb_u16be(b); size_t w = (len<out_sz-1)? len : (out_sz? out_sz-1:0); for(size_t i=0;i<w;i++) out[i]=(char)mb_u8(b); for(size_t i=w;i<len;i++) (void)mb_u8(b); if(out_sz) out[w]=0; }

typedef struct { uint8_t y; const uint8_t* blocks; } SectionInfo;

static bool nbt_skip_tag_payload(MBuf* b, int type);

static bool nbt_read_named_tag(MBuf* b, int* out_type, char* name, size_t name_sz){
  int t = mb_u8(b); if (t == -1) return false; *out_type = t; if (t == TAG_End){ if(name&&name_sz) name[0]='\0'; return true; } mb_str(b, name, name_sz); return true;
}

static bool nbt_read_sections_legacy(MBuf* b, SectionInfo* out_sections, int* out_count){
  // Expect a root compound possibly named "Level" in older chunks
  int t; char nm[64]; if (!nbt_read_named_tag(b, &t, nm, sizeof(nm))) return false;
  if (t != TAG_Compound) return false;
  // If name isn't Level, we still traverse until we find a compound named Level
  // Parse compound
  SectionInfo tmp[32]; int tmpc=0;
  while (1){ int ct; char name[64]; if (!nbt_read_named_tag(b,&ct,name,sizeof(name))) return false; if (ct==TAG_End) break; if (ct==TAG_Compound){
      // Recurse one level, look for Sections inside this compound
      size_t save_pos = b->pos;
      bool found=false;
      while (1){ int it; char iname[64]; if (!nbt_read_named_tag(b,&it,iname,sizeof(iname))) return false; if (it==TAG_End) break; if (it==TAG_List && strcmp(iname,"Sections")==0){
            int elem_type = mb_u8(b); uint32_t len = mb_u32be(b); if (elem_type != TAG_Compound){ // skip list
              // skip all elements payloads
              for (uint32_t i=0;i<len;i++){ if (!nbt_skip_tag_payload(b, elem_type)) return false; }
            } else {
              for (uint32_t i=0;i<len;i++){
                // Section compound
                SectionInfo si={0};
                while (1){ int st; char sn[64]; if (!nbt_read_named_tag(b,&st,sn,sizeof(sn))) return false; if (st==TAG_End) break; if (strcmp(sn,"Y")==0 && st==TAG_Byte){ si.y = mb_u8(b); }
                  else if (strcmp(sn,"Blocks")==0 && st==TAG_Byte_Array){ uint32_t blen = mb_u32be(b); if (blen==4096) { si.blocks = b->p + b->pos; } mb_skip(b, blen); }
                  else { if (!nbt_skip_tag_payload(b, st)) return false; }
                }
                if (si.blocks && tmpc < 32) tmp[tmpc++] = si;
              }
              found=true;
            }
      } else { if (!nbt_skip_tag_payload(b, it)) return false; }
      }
      if (!found){ b->pos = save_pos; // skip whole compound
        while (1){ int it2; char n2[64]; if (!nbt_read_named_tag(b,&it2,n2,sizeof(n2))) return false; if (it2==TAG_End) break; if (!nbt_skip_tag_payload(b, it2)) return false; }
      }
    } else { if (!nbt_skip_tag_payload(b, ct)) return false; }
  }
  for (int i=0;i<tmpc;i++) out_sections[i]=tmp[i]; *out_count = tmpc; return tmpc>0;
}

static bool nbt_skip_tag_payload(MBuf* b, int type){
  switch (type){
    case TAG_Byte: mb_skip(b,1); return true;
    case TAG_Short: mb_skip(b,2); return true;
    case TAG_Int: mb_skip(b,4); return true;
    case TAG_Long: mb_skip(b,8); return true;
    case TAG_Float: mb_skip(b,4); return true;
    case TAG_Double: mb_skip(b,8); return true;
    case TAG_Byte_Array: { uint32_t n=mb_u32be(b); mb_skip(b,n); return true; }
    case TAG_String: { uint16_t n=mb_u16be(b); mb_skip(b,n); return true; }
    case TAG_List: { int et=mb_u8(b); (void)et; uint32_t n=mb_u32be(b); for(uint32_t i=0;i<n;i++){ if (!nbt_skip_tag_payload(b, et)) return false; } return true; }
    case TAG_Compound: { while (1){ int t; char nm[4]; if (!nbt_read_named_tag(b,&t,nm,sizeof(nm))) return false; if (t==TAG_End) break; if (!nbt_skip_tag_payload(b,t)) return false; } return true; }
    case TAG_Int_Array: { uint32_t n=mb_u32be(b); mb_skip(b, n*4u); return true; }
    case TAG_Long_Array: { uint32_t n=mb_u32be(b); mb_skip(b, n*8u); return true; }
  }
  return false;
}

// Parse modern (1.13+) Sections list with Palette + BlockStates
static bool parse_sections_list_fn(MBuf* pb, World* world, bool* out_filled, uint32_t wx0, uint32_t wy0){
  int elem_type = mb_u8(pb);
  uint32_t len = mb_u32be(pb);
  if (elem_type != TAG_Compound) {
    for (uint32_t i=0;i<len;i++) nbt_skip_tag_payload(pb, elem_type);
    return false;
  }
  bool any=false;
  for (uint32_t si=0; si<len; si++){
    int y = 0; uint64_t* states = NULL; uint32_t states_len=0;
    const char* pal[4096]; uint32_t pal_len=0; char namebufs[4096][64];
    while (1){ int st; char sn[64]; if (!nbt_read_named_tag(pb,&st,sn,sizeof(sn))) return false; if (st==TAG_End) break;
      if (strcmp(sn,"Y")==0 && st==TAG_Byte){ y = (int)(int8_t)mb_u8(pb); }
      else if ((strcmp(sn,"block_states")==0 || strcmp(sn,"BlockStates")==0) && st==TAG_Long_Array){ uint32_t n=mb_u32be(pb); states=(uint64_t*)malloc((size_t)n*sizeof(uint64_t)); if(!states) return false; for(uint32_t i=0;i<n;i++) states[i]=mb_u64be(pb); states_len=n; }
      else if ((strcmp(sn,"Palette")==0 || strcmp(sn,"palette")==0) && st==TAG_List){ int et=mb_u8(pb); uint32_t plen=mb_u32be(pb); if (et!=TAG_Compound){ for(uint32_t i=0;i<plen;i++) nbt_skip_tag_payload(pb, et); }
        else { if (plen > 4096) plen = 4096; for (uint32_t pi=0; pi<plen; pi++){ while (1){ int pt; char pn[64]; if (!nbt_read_named_tag(pb,&pt,pn,sizeof(pn))) return false; if (pt==TAG_End) break; if (strcmp(pn,"Name")==0 && pt==TAG_String){ mb_str(pb, namebufs[pal_len], sizeof(namebufs[pal_len])); } else { nbt_skip_tag_payload(pb, pt); } } pal[pal_len] = namebufs[pal_len]; pal_len++; } }
      } else { nbt_skip_tag_payload(pb, st); }
    }
    if (pal_len > 0 && states && states_len > 0){
      int bpb = 4; while ((1u<<bpb) < pal_len) bpb++;
      uint64_t mask = (bpb >= 64) ? ~0ULL : ((1ULL<<bpb)-1ULL);
      for (int sy=0; sy<16; sy++) for (int z=0; z<16; z++) for (int x=0; x<16; x++){
        int i = (sy * 16 + z) * 16 + x; uint64_t bit = (uint64_t)i * (uint64_t)bpb; uint32_t li = (uint32_t)(bit / 64ULL); uint32_t bo = (uint32_t)(bit % 64ULL);
        uint64_t v = 0; if (li < states_len){ v = states[li] >> bo; if (bo + (uint32_t)bpb > 64 && li + 1 < states_len){ v |= states[li+1] << (64 - bo); } }
        uint32_t idx = (uint32_t)(v & mask); if (idx >= pal_len) idx = pal_len - 1; const char* bn = pal[idx]; VoxelType vt = VOXEL_AIR;
        if (bn){
          if (strstr(bn,"air")) vt = VOXEL_AIR;
          else if (strstr(bn,"stone")) vt = VOXEL_STONE;
          else if (strstr(bn,"grass_block")) vt = VOXEL_SOIL;
          else if (strstr(bn,"dirt")) vt = VOXEL_SOIL;
          else if (strstr(bn,"water")) vt = VOXEL_WATER;
          else if (strstr(bn,"lava")) vt = VOXEL_MAGMA;
          else if (strstr(bn,"sandstone")) vt = VOXEL_SANDSTONE;
          else if (strstr(bn,"sand")) vt = VOXEL_SAND;
          else if (strstr(bn,"gravel")) vt = VOXEL_STONE_GRANITE;
          else if (strstr(bn,"oak_log") || strstr(bn,"log")) vt = VOXEL_WOOD;
          else if (strstr(bn,"leaves")) vt = VOXEL_LEAVES;
          else if (strstr(bn,"glass")) vt = VOXEL_GLASS;
          else if (strstr(bn,"wool")) vt = VOXEL_WOOL;
          else if (strstr(bn,"brick")) vt = VOXEL_BRICK;
          else if (strstr(bn,"obsidian")) vt = VOXEL_OBSIDIAN;
          else if (strstr(bn,"diamond_ore")) vt = VOXEL_ORE_DIAMOND;
          else if (strstr(bn,"iron_ore")) vt = VOXEL_ORE_IRON;
          else if (strstr(bn,"gold_ore")) vt = VOXEL_ORE_GOLD;
          else if (strstr(bn,"coal_ore")) vt = VOXEL_ORE_COAL;
          else if (strstr(bn,"redstone_ore")) vt = VOXEL_ORE_HEMATITE;
          else if (strstr(bn,"lapis_ore")) vt = VOXEL_ORE_LAPIS;
          else if (strstr(bn,"ice")) vt = VOXEL_ICE;
          else if (strstr(bn,"snow_block")) vt = VOXEL_SNOW;
          else if (strstr(bn,"clay")) vt = VOXEL_CLAY;
          else if (strstr(bn,"netherrack")) vt = VOXEL_STONE_BASALT;
          else if (strstr(bn,"soul_sand")) vt = VOXEL_SAND;
          else if (strstr(bn,"glowstone")) vt = VOXEL_CRYSTAL;
        }
        world_set_voxel(world, wx0 + (uint32_t)x, wy0 + (uint32_t)z, (uint32_t)(y*16 + sy), vt);
      }
      any = true;
    }
    if (states) free(states);
  }
  if (out_filled && any) *out_filled = true;
  return true;
}

// Zlib/gzip chunk decompress
static bool decompress_chunk(const uint8_t* in, size_t in_len, int comp_type, uint8_t** out_buf, size_t* out_len){
  *out_buf = NULL; *out_len = 0;
  // Heuristic: start with 64KB and grow
  size_t cap = 256*1024; uint8_t* out = (uint8_t*)malloc(cap); if (!out) return false;
  z_stream strm; memset(&strm,0,sizeof(strm));
  int wbits = (comp_type==1)? (16+MAX_WBITS) : MAX_WBITS; // 1=gzip, 2=zlib
  if (inflateInit2(&strm, wbits) != Z_OK){ free(out); return false; }
  strm.next_in = (Bytef*)in; strm.avail_in = (uInt)in_len;
  int rc; size_t total=0;
  do {
    strm.next_out = out + total; strm.avail_out = (uInt)(cap - total);
    rc = inflate(&strm, Z_NO_FLUSH);
    total = cap - strm.avail_out;
    if (rc == Z_BUF_ERROR || (rc == Z_OK && strm.avail_out == 0)){
      cap *= 2; uint8_t* n = (uint8_t*)realloc(out, cap); if (!n){ inflateEnd(&strm); free(out); return false; } out = n;
    }
  } while (rc == Z_OK);
  if (rc != Z_STREAM_END){ inflateEnd(&strm); free(out); return false; }
  inflateEnd(&strm);
  *out_buf = out; *out_len = total; return true;
}

static VoxelType mc_block_to_voxel(uint8_t id) {
  switch (id) {
    case 0: return VOXEL_AIR; case 1: return VOXEL_STONE; case 2: return VOXEL_SOIL; case 3: return VOXEL_SOIL;
    case 8: case 9: return VOXEL_WATER; case 12: return VOXEL_SAND; case 13: return VOXEL_STONE_GRANITE;
    case 14: return VOXEL_ORE_GOLD; case 15: return VOXEL_ORE_IRON; case 16: return VOXEL_ORE_COAL;
    case 17: return VOXEL_WOOD; case 18: return VOXEL_LEAVES; case 20: return VOXEL_GLASS;
    case 21: return VOXEL_CRYSTAL_BLUE; case 22: return VOXEL_CRYSTAL_BLUE; case 24: return VOXEL_SANDSTONE;
    case 35: return VOXEL_WOOL; case 41: return VOXEL_GOLD; case 42: return VOXEL_ORE_IRON; case 45: return VOXEL_BRICK;
    case 49: return VOXEL_OBSIDIAN; case 56: return VOXEL_CRYSTAL; case 57: return VOXEL_CRYSTAL;
    case 73: return VOXEL_ORE_HEMATITE; case 79: return VOXEL_ICE; case 80: return VOXEL_SNOW; case 82: return VOXEL_CLAY;
    case 87: return VOXEL_STONE_BASALT; case 88: return VOXEL_SAND; case 89: return VOXEL_CRYSTAL;
    default: return VOXEL_STONE;
  }
}

// Read SpawnX, SpawnY, SpawnZ from level.dat (gzipped NBT)
static bool read_spawn_from_level_dat(const char* level_dat_path, int* sx, int* sy, int* sz){
  // Use popen gunzip -c to avoid zlib wrapper; read stream into dynamic buffer
  char cmd[1024]; snprintf(cmd, sizeof(cmd), "gunzip -c '%s'", level_dat_path);
  FILE* p = popen(cmd, "r"); if (!p) return false;
  size_t cap = 8192; size_t len = 0; uint8_t* buf = (uint8_t*)malloc(cap);
  if (!buf){ pclose(p); return false; }
  while (1){
    if (len == cap){ cap *= 2; uint8_t* n = (uint8_t*)realloc(buf, cap); if (!n){ free(buf); pclose(p); return false; } buf = n; }
    size_t nread = fread(buf + len, 1, cap - len, p);
    len += nread;
    if (nread == 0){ if (feof(p)) break; if (ferror(p)){ free(buf); pclose(p); return false; } }
  }
  pclose(p);
  if (len == 0){ free(buf); return false; }
  MBuf b = { buf, (size_t)len, 0 };
  int t = mb_u8(&b); if (t != TAG_Compound){ free(buf); return false; }
  char root[64]; mb_str(&b, root, sizeof(root)); (void)root;
  // Find Data compound
  bool ok=false; int outx=0,outy=64,outz=0;
  while (1){ int ct; char nm[64]; if (!nbt_read_named_tag(&b,&ct,nm,sizeof(nm))){ break; } if (ct==TAG_End) break;
    if (ct==TAG_Compound && strcmp(nm,"Data")==0){
      while (1){ int it; char in[64]; if (!nbt_read_named_tag(&b,&it,in,sizeof(in))) { break; }
        if (it==TAG_End) break;
        if (strcmp(in,"SpawnX")==0 && it==TAG_Int){ outx = (int)mb_u32be(&b); }
        else if (strcmp(in,"SpawnY")==0 && it==TAG_Int){ outy = (int)mb_u32be(&b); }
        else if (strcmp(in,"SpawnZ")==0 && it==TAG_Int){ outz = (int)mb_u32be(&b); }
        else { nbt_skip_tag_payload(&b,it); }
      }
      ok = true; break;
    } else { nbt_skip_tag_payload(&b, ct); }
  }
  free(buf);
  if (!ok) return false; *sx = outx; *sy = outy; *sz = outz; return true;
}

static bool load_chunk_into_world_at(const char* world_dir, int chunk_x, int chunk_z, World* world, uint32_t wx0, uint32_t wy0){
  int region_x = (chunk_x >= 0) ? (chunk_x / 32) : ((chunk_x - 31) / 32);
  int region_z = (chunk_z >= 0) ? (chunk_z / 32) : ((chunk_z - 31) / 32);
  int local_cx = chunk_x - region_x * 32;
  int local_cz = chunk_z - region_z * 32;
  char region_path[1024]; snprintf(region_path, sizeof(region_path), "%s/region/r.%d.%d.mca", world_dir, region_x, region_z);
  FILE* f = fopen(region_path, "rb"); if (!f){ fprintf(stderr, "Region not found: %s\n", region_path); return false; }
  uint8_t header[8192]; if (fread(header,1,8192,f) != 8192){ fclose(f); return false; }
  // Offsets table: 1024 entries of 4 bytes: 3 bytes offset (sector), 1 byte sector count
  int idx = local_cx + local_cz * 32;
  uint32_t loc = (header[idx*4] << 16) | (header[idx*4+1] << 8) | header[idx*4+2];
  uint8_t sectors = header[idx*4+3];
  if (loc == 0 || sectors == 0){ fclose(f); fprintf(stderr, "Chunk not present in region: %d,%d\n", chunk_x, chunk_z); return false; }
  uint32_t byte_offset = loc * 4096u;
  if (fseek(f, (long)byte_offset, SEEK_SET) != 0){ fclose(f); return false; }
  // Read 4-byte big-endian length
  int b0=fgetc(f), b1=fgetc(f), b2=fgetc(f), b3=fgetc(f);
  if (b0==EOF||b1==EOF||b2==EOF||b3==EOF){ fclose(f); return false; }
  uint32_t len = ((uint32_t)b0<<24) | ((uint32_t)b1<<16) | ((uint32_t)b2<<8) | (uint32_t)b3;
  int comp_type = fgetc(f); if (comp_type != 1 && comp_type != 2){ fclose(f); fprintf(stderr, "Unsupported compression type %d\n", comp_type); return false; }
  uint8_t* comp = (uint8_t*)malloc(len-1); if (!comp){ fclose(f); return false; }
  if (fread(comp,1,len-1,f) != len-1){ free(comp); fclose(f); return false; }
  fclose(f);
  uint8_t* decomp=NULL; size_t decomp_len=0; if (!decompress_chunk(comp, len-1, comp_type, &decomp, &decomp_len)){
    // Try other window bits heuristically if flagged wrong
    if (!decompress_chunk(comp, len-1, 2, &decomp, &decomp_len) && !decompress_chunk(comp, len-1, 1, &decomp, &decomp_len)){
      fprintf(stderr, "Decompress failed for chunk %d,%d (len=%u type=%d)\n", chunk_x, chunk_z, (unsigned)len, comp_type);
      free(comp);
      return false;
    }
  }
  free(comp);
  MBuf b = { decomp, decomp_len, 0 };
  // Try legacy sections first (pre-1.13, Blocks byte array)
  {
    MBuf bcopy = b;
    SectionInfo secs[32]; int sec_count=0; bool ok = nbt_read_sections_legacy(&bcopy, secs, &sec_count);
    if (ok && sec_count > 0){
      for (int i=0;i<sec_count;i++){
        int base_y = secs[i].y * 16;
        const uint8_t* blocks = secs[i].blocks; if (!blocks) continue;
        for (int sy=0; sy<16; sy++){
          for (int z=0; z<16; z++){
            for (int x=0; x<16; x++){
              int idxb = (sy * 16 + z) * 16 + x; // y-major then z then x
              uint8_t bid = blocks[idxb];
              VoxelType vt = mc_block_to_voxel(bid);
              world_set_voxel(world, wx0 + (uint32_t)x, wy0 + (uint32_t)z, (uint32_t)(base_y + sy), vt);
            }
          }
        }
      }
      free(decomp);
      return true;
    }
  }

  // Modern (1.13+) palette + BlockStates decoding
  b.pos = 0;
  int t; char rootnm[64]; if (!nbt_read_named_tag(&b,&t,rootnm,sizeof(rootnm)) || t != TAG_Compound){ free(decomp); return false; }
  bool filled_any=false;

  // Helper: parse a Sections list at current buffer position (see parse_sections_list_fn at file scope)

  while (1){ int ct; char nm[64]; if (!nbt_read_named_tag(&b,&ct,nm,sizeof(nm))) { free(decomp); return false; } if (ct==TAG_End) break;
    if (ct==TAG_List && (strcmp(nm,"sections")==0 || strcmp(nm,"Sections")==0)) {
      (void)parse_sections_list_fn(&b, world, &filled_any, wx0, wy0);
    } else if (ct==TAG_Compound && strcmp(nm,"Level")==0) {
      while (1){ int it; char in[64]; if (!nbt_read_named_tag(&b,&it,in,sizeof(in))) { free(decomp); return false; } if (it==TAG_End) break;
        if (it==TAG_List && (strcmp(in,"Sections")==0 || strcmp(in,"sections")==0)){
          (void)parse_sections_list_fn(&b, world, &filled_any, wx0, wy0);
        } else { nbt_skip_tag_payload(&b, it); }
      }
    } else { nbt_skip_tag_payload(&b, ct); }
  }
  /* done parsing modern */
  free(decomp);
  return filled_any;
  free(decomp);
  return true;
}

static bool load_spawn_chunk_into_world(const char* world_dir, int spawn_x, int spawn_z, World* world){
  int chunk_x = (spawn_x >= 0) ? (spawn_x / 16) : ((spawn_x - 15) / 16);
  int chunk_z = (spawn_z >= 0) ? (spawn_z / 16) : ((spawn_z - 15) / 16);
  if (load_chunk_into_world_at(world_dir, chunk_x, chunk_z, world, 0, 0)) return true;
  // Search nearby chunks in an expanding manhattan radius
  for (int r=1; r<=6; r++){
    for (int dz=-r; dz<=r; dz++){
      int dxs[2] = { -r, r };
      for (int k=0;k<2;k++){
        int dx = dxs[k];
        if (load_chunk_into_world_at(world_dir, chunk_x + dx, chunk_z + dz, world, 0, 0)) return true;
      }
    }
    for (int dx=-r+1; dx<=r-1; dx++){
      int dzs[2] = { -r, r };
      for (int k=0;k<2;k++){
        int dz = dzs[k];
        if (load_chunk_into_world_at(world_dir, chunk_x + dx, chunk_z + dz, world, 0, 0)) return true;
      }
    }
  }
  // Fallback: scan first present chunk in any region
  char region_dir[1024]; snprintf(region_dir, sizeof(region_dir), "%s/region", world_dir);
  DIR* d = opendir(region_dir);
  if (d){
    struct dirent* de;
    while ((de = readdir(d)) != NULL){
      if (!strstr(de->d_name, ".mca")) continue;
      int rx=0, rz=0; if (sscanf(de->d_name, "r.%d.%d.mca", &rx, &rz) != 2) continue;
      char region_path[1024]; snprintf(region_path, sizeof(region_path), "%s/%s", region_dir, de->d_name);
      FILE* f = fopen(region_path, "rb"); if (!f) continue;
      uint8_t header[8192]; size_t n = fread(header,1,8192,f); fclose(f); if (n != 8192) continue;
      for (int idx=0; idx<1024; idx++){
        uint32_t loc = (header[idx*4] << 16) | (header[idx*4+1] << 8) | header[idx*4+2];
        uint8_t sectors = header[idx*4+3];
        if (loc == 0 || sectors == 0) continue;
        int lcx = idx % 32; int lcz = idx / 32;
        int cx = rx*32 + lcx; int cz = rz*32 + lcz;
        if (load_chunk_into_world_at(world_dir, cx, cz, world, 0, 0)) { closedir(d); return true; }
      }
    }
    closedir(d);
  }
  return false;
}

static void ensure_dir(const char* path){ char cmd[1024]; snprintf(cmd,sizeof(cmd),"mkdir -p '%s'", path); (void)system(cmd); }

int main(int argc, char** argv){
  const char* mc_world_dir = (argc > 1) ? argv[1] : "models/modern-split";
  const char* out_worlds = (argc > 2) ? argv[2] : "worlds";
  const char* name = (argc > 3) ? argv[3] : "modern-split";

  char level_dat[1024]; snprintf(level_dat, sizeof(level_dat), "%s/level.dat", mc_world_dir);
  int sx=0, sy=64, sz=0; if (!read_spawn_from_level_dat(level_dat, &sx,&sy,&sz)){
    fprintf(stderr, "Failed to read spawn from %s\n", level_dat);
    return 1;
  }
  // Collect all present chunks and compute bounds
  typedef struct { int cx, cz; } ChunkCoord;
  ChunkCoord* chunks = NULL; size_t chunk_count = 0; size_t chunk_cap = 0;
  int min_cx = 0, max_cx = -1, min_cz = 0, max_cz = -1;
  {
    char region_dir[1024]; snprintf(region_dir, sizeof(region_dir), "%s/region", mc_world_dir);
    DIR* d = opendir(region_dir);
    if (!d){ fprintf(stderr, "No region directory: %s\n", region_dir); return 1; }
    struct dirent* de;
    while ((de = readdir(d)) != NULL){
      if (!strstr(de->d_name, ".mca")) continue;
      int rx=0, rz=0; if (sscanf(de->d_name, "r.%d.%d.mca", &rx, &rz) != 2) continue;
      char region_path[1024]; snprintf(region_path, sizeof(region_path), "%s/%s", region_dir, de->d_name);
      FILE* f = fopen(region_path, "rb"); if (!f) continue;
      uint8_t header[8192]; size_t n = fread(header,1,8192,f); fclose(f); if (n != 8192) continue;
      for (int idx=0; idx<1024; idx++){
        uint32_t loc = (header[idx*4] << 16) | (header[idx*4+1] << 8) | header[idx*4+2];
        uint8_t sectors = header[idx*4+3];
        if (loc == 0 || sectors == 0) continue;
        int lcx = idx % 32; int lcz = idx / 32;
        int cx = rx*32 + lcx; int cz = rz*32 + lcz;
        if (chunk_count == chunk_cap){ size_t nc = chunk_cap ? chunk_cap*2 : 256; ChunkCoord* n2 = (ChunkCoord*)realloc(chunks, nc*sizeof(ChunkCoord)); if (!n2){ free(chunks); closedir(d); fprintf(stderr, "alloc fail\n"); return 1; } chunks = n2; chunk_cap = nc; }
        chunks[chunk_count].cx = cx; chunks[chunk_count].cz = cz; chunk_count++;
        if (chunk_count == 1){ min_cx=max_cx=cx; min_cz=max_cz=cz; } else { if (cx<min_cx) min_cx=cx; if (cx>max_cx) max_cx=cx; if (cz<min_cz) min_cz=cz; if (cz>max_cz) max_cz=cz; }
      }
    }
    closedir(d);
    if (chunk_count == 0){ fprintf(stderr, "No chunks found in %s\n", region_dir); free(chunks); return 1; }
  }

  uint32_t width = (uint32_t)((max_cx - min_cx + 1) * 16);
  uint32_t height = (uint32_t)((max_cz - min_cz + 1) * 16);
  uint32_t depth = 256; // assume
  World* w = world_create(width, height, depth);
  if (!w){ free(chunks); fprintf(stderr, "World alloc failed for %ux%ux%u\n", width, height, depth); return 1; }
  // Load all chunks
  for (size_t i = 0; i < chunk_count; i++){
    int cx = chunks[i].cx;
    int cz = chunks[i].cz;
    uint32_t wx0 = (uint32_t)((cx - min_cx) * 16);
    uint32_t wy0 = (uint32_t)((cz - min_cz) * 16);
    (void)load_chunk_into_world_at(mc_world_dir, cx, cz, w, wx0, wy0);
  }
  free(chunks);
  ensure_dir(out_worlds);
  char outp[1024]; snprintf(outp, sizeof(outp), "%s/%s.world", out_worlds, name);
  if (!world_save(w, outp)){
    fprintf(stderr, "Save failed: %s\n", outp);
    world_destroy(w); return 1;
  }
  printf("Saved full world to %s (%ux%ux%u)\n", outp, width, height, depth);
  world_destroy(w);
  return 0;
}


