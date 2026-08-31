# CC0 Item Icons

| Folder | License | Source |
|--------|---------|--------|
| `shade_assorted/` | CC0 | [16x16 Assorted RPG Icons (Shade)](https://opengameart.org/content/16x16-assorted-rpg-icons) |
| `shade_weapons/` | CC0 | [16x16 Weapon RPG Icons (Shade)](https://opengameart.org/content/16x16-weapon-rpg-icons) |
| `kenney_voxel/` | CC0 | [Kenney Voxel Pack](https://kenney.nl/assets/voxel-pack) (subset) |
| `misc/bone_items.png` | CC0 | [Bone items (OpenGameArt)](https://opengameart.org/content/bone-items) |
| `misc/chikenwing.png` | CC0 | [16x16 item and weapon tiles](https://opengameart.org/content/16x16-item-and-weapon-tiles) |

## Rebuild runtime icons

```bash
python3 -m venv /tmp/verse_cc0_venv && /tmp/verse_cc0_venv/bin/pip install pillow
/tmp/verse_cc0_venv/bin/python3 scripts/build_item_icons.py
```

Outputs:
- `assets/items/icons/*.png` — one 16×16 icon per `ItemId`
- `assets/items/item_atlas.png` — row atlas
- `src/item_icon_data.inc` — embedded RGBA for `item_icon.c`
