# CC0 Settlement Assets

Voxel / tile assets for wilderness settlement construction (WFC town tiles).

## Included (downloaded)

| Folder | License | Source | Notes |
|--------|---------|--------|-------|
| `oga_buildings/` | CC0 | [Voxel Buildings (OpenGameArt)](https://opengameart.org/content/voxel-buildings) | MagicaVoxel `.vox` houses (13 models) — primary style source |
| `tarabaz_city/` | MIT (unrestricted) | [magical-voxel-3d-city-model](https://github.com/tarabaz/magical-voxel-3d-city-model) | City tiles (multi-model); kept as source, not in runtime set |
| `kenney_voxel/` | CC0 | [Kenney Voxel Pack](https://opengameart.org/content/voxel-pack) | 2D tile/item spritesheets (reference / UI), not MagicaVoxel |

## Manual itch.io downloads (CC0, name-your-own-price)

itch.io gates free downloads behind a purchase flow. Drop the unpacked files into the matching folder:

| Folder | Pack | URL |
|--------|------|-----|
| `padadu06/` | Voxel pack 06 – Medieval (modular walls/roofs) | https://padadu.itch.io/voxel-pack-06-medieval-assets |
| `kytric/` | Voxel Path And Terrain (prefer `PathAndTerrain-vox.zip`) | https://kytric.itch.io/voxel-path-and-terrain |
| `felix_medieval/` | Medieval Voxel Assets | https://felixstrefter.itch.io/medieval-voxel-assets |
| `joselu_medieval/` | Medieval Voxel Buildings | https://joselugames.itch.io/medieval-voxel-buildings |
| `makovice_ruins/` | Ancient Ruins (`RuinsVox.zip` tip tier) | https://makovice.itch.io/ancient-ruins-assset-pack |
| `makovice_cold/` | Cold Biome (`ColdBiomVox.zip` tip tier) | https://makovice.itch.io/cold-biome-voxel-asset-pack |
| `quaternius_village/` | Medieval Village MegaKit (mesh, CC0; voxelize later) | https://quaternius.itch.io/medieval-village-megakit |

## Runtime

- **Home-drop (universe 0,0, scale 1):** one procedural hunter's shack (quest note inside).
- **Other settlements:** typed prefabs from `models/buildings/*.vbuild` — hut, cottage, house, shop, tower, hall, manor, castle — mixed by scale. Procedural boxes if a prefab file is missing.
- **Regenerate prefabs:** `python3 scripts/prepare_settlement_buildings.py`
