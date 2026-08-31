#!/usr/bin/env python3
"""Build curated 16x16 item icons from CC0 packs into assets/items/ and embed RGBA."""

from __future__ import annotations

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CC0 = ROOT / "models" / "cc0_items"
OUT = ROOT / "assets" / "items"
ICONS = OUT / "icons"
CHIKEN = OUT / "chiken_tiles"
INC = ROOT / "src" / "item_icon_data.inc"

SIZE = 16

# ItemId order must match src/item.h (ITEM_NONE .. ITEM_BRICK).
ITEM_NAMES = [
    "none",
    "stone_block",
    "wood_block",
    "food",
    "potion",
    "weapon_club",
    "core_armor_leather",
    "leggings_leather",
    "helmet_leather",
    "gauntlets_leather",
    "ring_copper",
    "necklace_bone",
    "wool",
    "hunters_note",
    "weapon_dagger",
    "weapon_spear",
    "weapon_sword",
    "weapon_staff",
    "core_armor_iron",
    "leggings_iron",
    "helmet_iron",
    "gauntlets_iron",
    "hide",
    "raw_meat",
    "bone",
    "fang",
    "venom_sac",
    "slime_gel",
    "feather",
    "egg",
    "clay",
    "iron_ore",
    "copper_ore",
    "tin_ore",
    "coal",
    "stick",
    "iron_ingot",
    "copper_ingot",
    "tin_ingot",
    "brick",
]


def crop16(im: Image.Image, c: int, r: int, cell: int = 16) -> Image.Image:
    return im.crop((c * cell, r * cell, c * cell + cell, r * cell + cell)).convert("RGBA")


def kenney(name: str) -> Image.Image:
    im = Image.open(CC0 / "kenney_voxel" / f"{name}.png").convert("RGBA")
    return im.resize((SIZE, SIZE), Image.Resampling.BOX)


def shade_weapon(c: int, r: int) -> Image.Image:
    return crop16(Image.open(CC0 / "shade_weapons" / "iron-weapons.png"), c, r)


def shade_armour(c: int, r: int) -> Image.Image:
    return crop16(Image.open(CC0 / "shade_assorted" / "armours.png"), c, r)


def shade_potion(c: int, r: int) -> Image.Image:
    return crop16(Image.open(CC0 / "shade_assorted" / "potions.png"), c, r)


def bone(i: int) -> Image.Image:
    im = Image.open(CC0 / "misc" / "bone_items.png").convert("RGBA")
    tile = im.crop((i * 32, 0, i * 32 + 32, 32))
    return tile.resize((SIZE, SIZE), Image.Resampling.NEAREST)


def chiken(idx: int) -> Image.Image:
    matches = sorted(CHIKEN.glob(f"{idx:03d}_*.png"))
    if not matches:
        raise FileNotFoundError(f"chiken tile {idx}")
    return Image.open(matches[0]).convert("RGBA").resize((SIZE, SIZE), Image.Resampling.NEAREST)


def placeholder(r: int, g: int, b: int) -> Image.Image:
    im = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = im.load()
    for y in range(SIZE):
        for x in range(SIZE):
            edge = x == 0 or y == 0 or x == SIZE - 1 or y == SIZE - 1
            if edge:
                px[x, y] = (20, 20, 20, 220)
            elif 3 <= x < 13 and 3 <= y < 13:
                px[x, y] = (r, g, b, 255)
    return im


def build_icons() -> list[Image.Image]:
    ICONS.mkdir(parents=True, exist_ok=True)

    # Ensure dirt exists for clay.
    dirt = CC0 / "kenney_voxel" / "dirt.png"
    if not dirt.exists():
        src = ROOT / "models" / "cc0_settlements" / "kenney_voxel" / "PNG" / "Tiles" / "dirt.png"
        if src.exists():
            dirt.write_bytes(src.read_bytes())

    icons: dict[str, Image.Image] = {
        "none": Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)),
        "stone_block": kenney("stone"),
        "wood_block": kenney("wood"),
        "food": kenney("stew"),
        "potion": shade_potion(3, 0),
        "weapon_club": shade_weapon(3, 10),
        "core_armor_leather": shade_armour(1, 0),
        "leggings_leather": shade_armour(2, 1),
        "helmet_leather": shade_armour(0, 1),
        "gauntlets_leather": shade_armour(4, 0),
        "ring_copper": chiken(138),
        "necklace_bone": bone(0),
        "wool": chiken(182),
        "hunters_note": chiken(41),
        "weapon_dagger": shade_weapon(6, 10),
        "weapon_spear": shade_weapon(15, 2),
        "weapon_sword": kenney("sword_iron"),
        "weapon_staff": shade_weapon(18, 2),
        "core_armor_iron": shade_armour(4, 8),
        "leggings_iron": shade_armour(5, 9),
        "helmet_iron": shade_armour(3, 8),
        "gauntlets_iron": shade_armour(6, 9),
        "hide": chiken(183),
        "raw_meat": chiken(143),
        "bone": chiken(174),
        "fang": bone(3),
        "venom_sac": shade_potion(8, 2),
        "slime_gel": chiken(178),
        "feather": chiken(134),  # pale shard / plume stand-in
        "egg": chiken(202),  # oval shell stand-in
        "clay": kenney("dirt") if dirt.exists() else kenney("stone"),
        "iron_ore": kenney("ore_iron"),
        "copper_ore": kenney("ore_gold"),
        "tin_ore": kenney("ore_silver"),
        "coal": kenney("ore_coal"),
        "stick": chiken(107),
        "iron_ingot": chiken(30),
        "copper_ingot": chiken(29),
        "tin_ingot": chiken(31),
        "brick": kenney("brick_red"),
    }

    ordered: list[Image.Image] = []
    for name in ITEM_NAMES:
        im = icons.get(name)
        if im is None:
            im = placeholder(180, 180, 180)
        im = im.convert("RGBA").resize((SIZE, SIZE), Image.Resampling.NEAREST)
        im.save(ICONS / f"{name}.png")
        ordered.append(im)

    # Atlas: one row, ITEM_COUNT tiles.
    atlas = Image.new("RGBA", (SIZE * len(ordered), SIZE), (0, 0, 0, 0))
    for i, im in enumerate(ordered):
        atlas.paste(im, (i * SIZE, 0), im)
    atlas.save(OUT / "item_atlas.png")
    # BMP without alpha for tooling; runtime uses embedded RGBA.
    atlas.convert("RGB").save(OUT / "item_atlas.bmp")
    return ordered


def write_inc(icons: list[Image.Image]) -> None:
    lines = [
        "/* Auto-generated by scripts/build_item_icons.py — do not edit. */",
        f"#define ITEM_ICON_SIZE {SIZE}",
        f"#define ITEM_ICON_COUNT {len(icons)}",
        "static const unsigned char s_item_icon_rgba[ITEM_ICON_COUNT][ITEM_ICON_SIZE * ITEM_ICON_SIZE * 4] = {",
    ]
    for im in icons:
        px = list(im.convert("RGBA").getdata())
        flat = ", ".join(str(c) for p in px for c in p)
        lines.append(f"  {{ {flat} }},")
    lines.append("};")
    INC.write_text("\n".join(lines) + "\n")


def main() -> None:
    if not CHIKEN.exists() or not any(CHIKEN.glob("*.png")):
        raise SystemExit(
            "Missing assets/items/chiken_tiles — extract chikenwing tiles first "
            "(see models/cc0_items/README.md)."
        )
    icons = build_icons()
    write_inc(icons)
    print(f"Wrote {len(icons)} icons → {ICONS} and {INC}")


if __name__ == "__main__":
    main()
