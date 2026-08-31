#ifndef MOB_MODELS_H
#define MOB_MODELS_H

#include <stdbool.h>

#include "actor.h"
#include "world.h"

// Sub-voxel mob models: a nested World whose section is the model's bounding box
// rounded up to a multiple of 32 per axis, matching MATERIAL_WORLD_SIZE. One parent
// voxel is MOB_MODEL_SUBVOXELS sub-voxels. The Mud Golem is two voxels tall; the
// bird occupies one.
//
// This file is plain C with no SDL, so the registry can be tested headlessly. The
// isometric sprite bake and the first-person mesh pass live in the renderers.

#define MOB_MODEL_SUBVOXELS 32

typedef enum
{
  MOB_MODEL_MUD_GOLEM = 0,
  MOB_MODEL_BIRD,
  MOB_MODEL_SHEEP,
  MOB_MODEL_CHICKEN,
  MOB_MODEL_BAT,
  MOB_MODEL_DEER,
  MOB_MODEL_LIZARD,
  MOB_MODEL_SPIDER,
  MOB_MODEL_SLIME,
  MOB_MODEL_FLESH_WALKER,
  MOB_MODEL_VILLAGER,
  MOB_MODEL_COUNT
} MobModelKind;

typedef struct
{
  MobModelKind kind;
  World *world; // section-sized, owned by the registry
  int section_x, section_y, section_z; // sub-voxels, each a multiple of 32
  bool loaded;
} MobModel;

// VERSE_MOB_MODELS=0 keeps the old procedural shapes so the two can be compared
// without a rebuild. On by default.
bool mob_models_enabled(void);

// Register models_dir (NULL means "models"). Idempotent and cheap — each .world
// loads on first mob_models_get. A missing file leaves that slot unloaded.
bool mob_models_init(const char *models_dir);
void mob_models_shutdown(void);

// The shared model for a kind, or NULL if that file was missing / not yet inited.
const MobModel *mob_models_get(MobModelKind kind);

// The model a renderer should use for this actor: birds share the shrike, a
// "Mud Golem" uses the clay body. Anything else returns NULL so the caller keeps
// its current drawing.
const MobModel *mob_models_for_actor(const Actor *a);

#endif // MOB_MODELS_H
