#include "mob_models.h"

#include "mob_ai.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MobModel g_models[MOB_MODEL_COUNT];
static bool g_slot_attempted[MOB_MODEL_COUNT];
static char g_models_dir[512];
static bool g_inited = false;

static const char *g_filenames[MOB_MODEL_COUNT] = {
    "mud_golem.world",
    "bird_shrike.world",
    "sheep.world",
    "chicken.world",
    "bat.world",
    "anim/deer.world",
    "lizardmonitorpackage/lizardmonitor.world",
    "VoxelDungeonSet/Vox/spider.world",
    "VoxelDungeonSet/Vox/slime.world",
    "flesh_walker.world",
    "VoxelDungeonSet/Vox/human_base.world",
};

bool mob_models_enabled(void)
{
  static int cached = -1;
  if (cached < 0)
  {
    const char *env = getenv("VERSE_MOB_MODELS");
    cached = (env && env[0] == '0') ? 0 : 1;
  }
  return cached != 0;
}

static int round_up_32(int n)
{
  if (n <= 0)
    return MOB_MODEL_SUBVOXELS;
  return ((n + MOB_MODEL_SUBVOXELS - 1) / MOB_MODEL_SUBVOXELS) * MOB_MODEL_SUBVOXELS;
}

static bool load_one(MobModel *slot, MobModelKind kind, const char *dir, const char *filename)
{
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", dir, filename);

  World *src = world_create(1, 1, 1);
  if (!src)
    return false;
  if (!world_load(src, path))
  {
    world_destroy(src);
    return false;
  }

  const int src_w = (int)src->width;
  const int src_h = (int)src->height;
  const int src_d = (int)src->depth;
  const int sec_x = round_up_32(src_w);
  const int sec_y = round_up_32(src_h);
  const int sec_z = round_up_32(src_d);
  const int ox = (sec_x - src_w) / 2;
  const int oy = (sec_y - src_h) / 2;

  World *dst = world_create((uint32_t)sec_x, (uint32_t)sec_y, (uint32_t)sec_z);
  if (!dst)
  {
    world_destroy(src);
    return false;
  }

  for (int z = 0; z < src_d; z++)
  {
    for (int y = 0; y < src_h; y++)
    {
      for (int x = 0; x < src_w; x++)
      {
        const Voxel *v = world_voxel_cptr_fast(src, x, y, z);
        if (!v || v->type == VOXEL_AIR)
          continue;
        *world_voxel_ptr_fast(dst, ox + x, oy + y, z) = *v;
      }
    }
  }

  world_destroy(src);
  slot->kind = kind;
  slot->world = dst;
  slot->section_x = sec_x;
  slot->section_y = sec_y;
  slot->section_z = sec_z;
  slot->loaded = true;
  return true;
}

bool mob_models_init(const char *models_dir)
{
  // Directory only — each .world loads on first mob_models_get so the first bird on screen does
  // not deserialize golem+deer+lizard+spider+slime in the same frame.
  if (g_inited)
    return true;

  const char *dir = (models_dir && models_dir[0]) ? models_dir : "models";
  strncpy(g_models_dir, dir, sizeof(g_models_dir) - 1);
  g_models_dir[sizeof(g_models_dir) - 1] = '\0';
  memset(g_models, 0, sizeof(g_models));
  memset(g_slot_attempted, 0, sizeof(g_slot_attempted));
  for (int i = 0; i < MOB_MODEL_COUNT; i++)
    g_models[i].kind = (MobModelKind)i;
  g_inited = true;
  return true;
}

void mob_models_shutdown(void)
{
  for (int i = 0; i < MOB_MODEL_COUNT; i++)
  {
    if (g_models[i].world)
    {
      world_destroy(g_models[i].world);
      g_models[i].world = NULL;
    }
    g_models[i].loaded = false;
  }
  memset(g_slot_attempted, 0, sizeof(g_slot_attempted));
  g_models_dir[0] = '\0';
  g_inited = false;
}

const MobModel *mob_models_get(MobModelKind kind)
{
  if (kind < 0 || kind >= MOB_MODEL_COUNT)
    return NULL;
  if (!g_inited)
    mob_models_init(NULL);
  if (!g_slot_attempted[kind])
  {
    g_slot_attempted[kind] = true;
    if (!load_one(&g_models[kind], kind, g_models_dir, g_filenames[kind]))
      fprintf(stderr, "[mob_models] missing or unreadable: %s/%s\n", g_models_dir,
              g_filenames[kind]);
  }
  if (!g_models[kind].loaded || !g_models[kind].world)
    return NULL;
  return &g_models[kind];
}

const MobModel *mob_models_for_actor(const Actor *a)
{
  if (!a)
    return NULL;
  if (mob_actor_mesh_name(a))
    return NULL;
  if (mob_actor_is_bat(a))
    return mob_models_get(MOB_MODEL_BAT);
  if (mob_actor_is_bird(a))
    return mob_models_get(MOB_MODEL_BIRD);
  if (mob_actor_is_deer(a))
    return mob_models_get(MOB_MODEL_DEER);
  if (mob_actor_is_lizard(a))
    return mob_models_get(MOB_MODEL_LIZARD);
  if (mob_actor_is_spider(a))
    return mob_models_get(MOB_MODEL_SPIDER);
  if (mob_actor_is_slime(a))
    return mob_models_get(MOB_MODEL_SLIME);
  if (mob_actor_is_livestock(a))
  {
    const MobActor *mob = (const MobActor *)a->extra_data;
    if (mob && mob->mob_type == MOB_TYPE_SHEEP)
      return mob_models_get(MOB_MODEL_SHEEP);
    if (mob && mob->mob_type == MOB_TYPE_CHICKEN)
      return mob_models_get(MOB_MODEL_CHICKEN);
  }
  if (mob_actor_is_villager(a))
    return mob_models_get(MOB_MODEL_VILLAGER);
  if (a->name[0] && strcmp(a->name, "Mud Golem") == 0)
    return mob_models_get(MOB_MODEL_MUD_GOLEM);
  if (a->name[0] && strcmp(a->name, "Flesh Walker") == 0)
    return mob_models_get(MOB_MODEL_FLESH_WALKER);
  return NULL;
}
