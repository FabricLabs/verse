#ifndef VERSE_MOB_LOOT_H
#define VERSE_MOB_LOOT_H

#include "actor.h"
#include "mob_ai.h"
#include <stdbool.h>

// Equip a newly created mob with role-appropriate gear.
void mob_actor_apply_loadout(MobActor *mob);

// Roll the corpse loot table into inventory once (equipped gear stays worn until looted).
void mob_actor_generate_corpse_loot(Actor *actor);

bool mob_actor_corpse_loot_ready(const Actor *actor);

#endif // VERSE_MOB_LOOT_H
