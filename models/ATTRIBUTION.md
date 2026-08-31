# Model Attribution

Provenance and licensing for third-party assets in `models/`.

## Mud Golem (`mud_golem.vox` / `mud_golem.world`)

- **Source**: "55 voxel creatures" by kooow — https://opengameart.org/content/55-voxel-creatures
  (derived in turn from https://opengameart.org/content/lava-golem)
- **Original file**: `vox_files/lava_golem.vox`
- **License**: CC0 (public domain)
- **Modifications**: converted from the pack's raw voxel grid to MagicaVoxel `.vox`,
  colour legend voxels removed, downscaled from 75x30x73 to 43x26x64 (two parent
  voxels tall at 32 sub-voxels per voxel), and recoloured from lava to wet clay so
  it reads as the "lumbering construct of wet clay" the Mud Golem is described as
  in `mob_ai.c`.

## Bird (`bird_shrike.vox` / `bird_shrike.world`)

- **Source**: "55 voxel creatures" by kooow — https://opengameart.org/content/55-voxel-creatures
  (derived in turn from https://opengameart.org/content/bird-yellow-billed-shrike)
- **Original file**: `vox_files/yellow-billed_shrike.vox`
- **License**: CC0 (public domain)
- **Modifications**: converted to MagicaVoxel `.vox`, colour legend voxels removed,
  and downscaled from 91x81x85 to 16x25x26. The source pose has the wings raised
  mid-beat, which suits the `BIRD_PHASE_TAKEOFF` and `BIRD_PHASE_CIRCLE` states.

Neither model is animated. No CC0 or otherwise freely redistributable *animated*
voxel bird or golem was available; every animated pack found (durs.co, VOX FOX,
GameDev Market, NETOVOXEL) is paid. Multi-frame `.vox` animation is supported by
the format — see `anim/deer.vox` and `anim/T-Rex.vox` — so frames can be added later.

## Sheep (`sheep.vox` / `sheep.world` + `models/poly/sheep.vmesh`)

- **Voxel source**: Quaternius Farm Animal Pack sheep OBJ, voxelized with
  `scripts/voxelize_mesh_to_vox.py` (CC0)
  https://opengameart.org/content/lowpoly-animated-farm-animal-pack
- **Animated poly**: same pack's `Sheep.blend` (Idle, Walk, Run, Jump, Death),
  baked by `scripts/bake_gltf_vmesh.py`
- **License**: CC0 1.0 Universal

## Chicken (`chicken.vox` / `chicken.world` + `models/poly/chick.vmesh`)

- **Voxel source**: "55 voxel creatures" rooster — https://opengameart.org/content/55-voxel-creatures
  (`vox_files/rooster.vox`), imported via `scripts/import_oga_vox.py`
- **Animated poly**: Kenney Cube Pets `animal-chick.glb` (Idle/Walk/Run)
  https://opengameart.org/content/cube-pets
- **License**: CC0 1.0 Universal

## Bat (`bat.vox` / `bat.world` + `models/poly/bat.vmesh`)

- **Voxel source**: "55 voxel creatures" wingless bat mutant
  (`vox_files/wingless_bat_mutant.vox`), imported via `scripts/import_oga_vox.py`
- **Animated poly**: Vampire Bat by J-Toastie — https://opengameart.org/content/vampire-bat-animated
  (`bat_v5.blend`: Idle, Flying, Death, Attack)
- **License**: CC0 1.0 Universal

Sheep, chicken and bat wilderness spawns bind the polygon mesh so locomotion and
combat drive Walk/Idle/Flying/Death. The MagicaVoxel `.world` files remain as the
fallback when no mesh is bound (and for headless model tests).

## Kenney Cube Pets remainder (`models/poly/{bunny,cat,…}.vmesh`)

- **Source**: Cube Pets by Kenney — https://kenney.nl/assets/cube-pets
  (also https://opengameart.org/content/cube-pets)
- **Vendored**: `models/cc0_cube_pets/` (full pack) + staged GLBs under `models/poly/`
- **License**: CC0 1.0 Universal
- **Baked**: Walk/Run clips via `scripts/bake_gltf_vmesh.py` (static Idle dropped)
- **In-game bindings**:
  - chick → chicken; cow/pig/hog → settlement + wild livestock
  - bunny → rabbit; giraffe → deer; elephant → rare wild elephant
  - parrot → sparrow/gull birds; monkey → lizard
  - lion/tiger → spider predators; caterpillar → slime
  - dog/cat → camp pets beside villagers
  - `fish.vmesh` is baked/registered but unbound (no swim AI yet)

## Gobkit Free Minions (`models/poly/minion-*.vmesh`)

- **Source**: Gobkit Free Minions — https://gobkit.itch.io/gobkit-free-minions
  (`https://gobkit.com/freebies/minion/minion-*.glb`)
- **Vendored**: `models/cc0_minions/` + `models/poly/minion-*.glb`
- **License**: CC0 1.0 Universal
- **Animations**: single `movement` timeline sliced to Idle / Attack / Death
- **In-game bindings**: settlement villagers (a/d male, b female, c child)

## Goleling (`models/poly/goleling.gltf` / `goleling.vmesh`)

- **Source**: Ultimate Monsters by Quaternius — https://quaternius.com/packs/ultimatemonsters.html
  (`Flying/glTF/Goleling_Evolved.gltf`)
- **License**: CC0 1.0 Universal (public domain)
- **Animations**: Death, Fast_Flying, Flying_Idle, Headbutt, HitReact, No, Punch, Yes
- **Modifications**: decimated to 1800 triangles, scaled to 1.85 voxels tall, rotated
  so +X is forward, and baked to evaluated vertex frames in `.vmesh` by
  `scripts/bake_gltf_vmesh.py`.

## Pigeon (`models/poly/pigeon.gltf` / `pigeon.vmesh`)

- **Source**: Ultimate Monsters by Quaternius — https://quaternius.com/packs/ultimatemonsters.html
  (`Flying/glTF/Pigeon.gltf`)
- **License**: CC0 1.0 Universal (public domain)
- **Animations**: Death, Fast_Flying, Flying_Idle, Headbutt, HitReact, No, Punch, Yes
- **Modifications**: same bake as the Goleling, scaled to 0.55 voxels tall.

Home-world spawn binds `Flying_Idle` while standing or perched and `Fast_Flying`
while moving. Rebuild the `.vmesh` files with:

```sh
blender --background --python scripts/bake_gltf_vmesh.py -- \
  models/poly/goleling.gltf models/poly/goleling.vmesh --height 1.85
blender --background --python scripts/bake_gltf_vmesh.py -- \
  models/poly/pigeon.gltf models/poly/pigeon.vmesh --height 0.55
```

## Regenerating

```sh
python3 scripts/import_oga_vox.py <pack>/lava_golem.vox models/mud_golem.vox --height 64 --palette mud
python3 scripts/import_oga_vox.py <pack>/yellow-billed_shrike.vox models/bird_shrike.vox --height 26
python3 scripts/import_oga_vox.py <pack>/rooster.vox models/chicken.vox --height 22
python3 scripts/import_oga_vox.py <pack>/wingless_bat_mutant.vox models/bat.vox --height 18
blender --background --python scripts/voxelize_mesh_to_vox.py -- \
  Sheep.obj models/sheep.vox --height 24 --voxel-size 0.035
make asset-converter && ./asset-converter models models --only-vox
blender --background --python scripts/bake_gltf_vmesh.py -- \
  models/poly/sheep.blend models/poly/sheep.vmesh --height 1.4
blender --background --python scripts/bake_gltf_vmesh.py -- \
  models/poly/chick.glb models/poly/chick.vmesh --height 0.55
blender --background --python scripts/bake_gltf_vmesh.py -- \
  models/poly/bat.blend models/poly/bat.vmesh --height 0.7
```

## Flesh Walker (`flesh_walker.vox` / `.world` + `models/poly/flesh_walker.vmesh`)

- **Skeleton source**: Kenney Animated Characters 3 — CC0 1.0
  https://opengameart.org/content/animated-characters-3
  (`Model/characterMedium.fbx` + `Animations/{idle,run,jump}.fbx`)
  Vendored under `models/cc0_skeleton/kenney_animated_characters_3/`.
- **Also staged**: Quaternius Universal Animation Library (CC0 humanoid retarget rig)
  https://quaternius.com/packs/universalanimationlibrary.html
  mirror `models/cc0_skeleton/quaternius_ual/`; Gobkit free minion
  `models/cc0_skeleton/gobkit_minion_a01.glb` (CC0).
- **Procedural voxels**: `scripts/skeleton_voxel_flesh.py` stamps `VOXEL_BONE`
  capsules along every deforming bone and a thicker `VOXEL_FLESH` shell, then
  bakes Idle/Walk/Run/Jump vertex frames for the poly renderer.
- **License**: CC0 1.0 Universal (Kenney / Quaternius / Gobkit source assets).

```sh
blender --background --python scripts/skeleton_voxel_flesh.py -- \
  models/cc0_skeleton/kenney_animated_characters_3 \
  models/flesh_walker.vox models/poly/flesh_walker.vmesh --height 56 --mesh-height 1.85
make asset-converter && ./asset-converter models models --only-vox
```

## Pre-existing assets

`chr_knight`, `monu*`, `castle`, `doom`, `teapot`, `room`, `menger`, `cars` and
`anim/{deer,T-Rex}` come from the MagicaVoxel sample set,
https://github.com/ephtracy/voxel-model.

## Settlement packs (`models/cc0_settlements/`)

See `models/cc0_settlements/README.md` for the full inventory and itch.io download list.
Game-ready downscales live in `models/buildings/` (see that folder's README / `manifest.json`).

- **Voxel Buildings** (`oga_buildings/*.vox` → most of `models/buildings/*.{vox,vbuild}`) — mehrasaur / OpenGameArt, CC0
  https://opengameart.org/content/voxel-buildings
- **Kenney Voxel Pack** (`kenney_voxel/`) — Kenney.nl via OpenGameArt, CC0
  https://opengameart.org/content/voxel-pack
- **MagicaVoxel city models** (`tarabaz_city/*.vox`) — tarabaz, MIT (source pack; not in runtime set)
  https://github.com/tarabaz/magical-voxel-3d-city-model
- **Castle** (`models/buildings/castle.{vox,vbuild}` from `models/castle.vox`) — MagicaVoxel sample set

