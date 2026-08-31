#!/usr/bin/env python3
"""Downscale MagicaVoxel settlement packs into game-sized building prefabs.

Writes:
  models/buildings/<name>.vox     — MagicaVoxel (palette remapped toward VERSE colors)
  models/buildings/<name>.vbuild  — compact typed prefab for settlement_stamp()
  models/buildings/README.md
  models/buildings/manifest.json

Usage (from repo root):
    python3 scripts/prepare_settlement_buildings.py
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "models" / "buildings"

# VoxelType ids from src/voxel.h (must stay in sync).
VT = {
    "STONE": 3,
    "GRAVEL": 8,
    "SOIL": 18,
    "WOOD": 35,
    "WOOD_OAK": 36,
    "WOOD_BIRCH": 38,
    "WOOD_PINE": 39,
    "GLASS": 129,
    "BRICK": 130,
    "CLAY": 133,
    "PLANK": 142,
    "THATCH": 143,
    "STRAW": 144,
    "COBBLE": 145,
    "PLASTER": 146,
    "TERRACOTTA": 147,
    "ADOBE": 148,
}

# VERSE construction palette (from VOXEL_COLOR_* in voxel.h).
PALETTE = [
    (VT["STONE"], 0x80, 0x80, 0x80),
    (VT["GRAVEL"], 0x8C, 0x82, 0x78),
    (VT["SOIL"], 0x64, 0x3C, 0x1E),
    (VT["WOOD"], 0x8B, 0x5A, 0x2B),
    (VT["WOOD_OAK"], 0x8B, 0x5A, 0x2B),
    (VT["WOOD_BIRCH"], 0xCD, 0xBE, 0x96),
    (VT["WOOD_PINE"], 0x6E, 0x4B, 0x2D),
    (VT["GLASS"], 0xC8, 0xDC, 0xF0),
    (VT["BRICK"], 0xB2, 0x22, 0x22),
    (VT["CLAY"], 0xA0, 0xAA, 0xB4),
    (VT["PLANK"], 0xC4, 0xA5, 0x74),
    (VT["THATCH"], 0xC8, 0xA8, 0x5A),
    (VT["STRAW"], 0xD2, 0xB8, 0x6A),
    (VT["COBBLE"], 0x7A, 0x7A, 0x72),
    (VT["PLASTER"], 0xE8, 0xE0, 0xD0),
    (VT["TERRACOTTA"], 0xC4, 0x5A, 0x2A),
    (VT["ADOBE"], 0xB8, 0x89, 0x5A),
]

# (name, source, footprint, building_type, note)
# building_type matches SettlementBuildingType names in settlement.h
CATALOG = [
    ("hut_reed", "models/cc0_settlements/oga_buildings/12.vox", 8, "hut", "Small sparse hut"),
    ("hut_thatch", "models/cc0_settlements/oga_buildings/13.vox", 8, "hut", "Small thatched hut"),
    ("cottage_a", "models/cc0_settlements/oga_buildings/1.vox", 9, "cottage", "Cottage with pitched roof"),
    ("cottage_b", "models/cc0_settlements/oga_buildings/2.vox", 9, "cottage", "Cottage variant"),
    ("cottage_c", "models/cc0_settlements/oga_buildings/3.vox", 9, "cottage", "Cottage variant"),
    ("cottage_d", "models/cc0_settlements/oga_buildings/4.vox", 9, "cottage", "Cottage variant"),
    ("house_a", "models/cc0_settlements/oga_buildings/5.vox", 11, "house", "Two-storey house"),
    ("house_b", "models/cc0_settlements/oga_buildings/8.vox", 11, "house", "Village house"),
    ("house_c", "models/cc0_settlements/oga_buildings/9.vox", 11, "house", "Village house"),
    ("shop_a", "models/cc0_settlements/oga_buildings/7.vox", 12, "shop", "Wide shop / storefront"),
    ("tower_a", "models/cc0_settlements/oga_buildings/10.vox", 12, "tower", "House with tower accent"),
    ("tower_tall", "models/cc0_settlements/oga_buildings/6.vox", 12, "tower", "Tall narrow tower-house"),
    ("hall_a", "models/cc0_settlements/oga_buildings/7.vox", 14, "hall", "Communal hall"),
    ("manor", "models/cc0_settlements/oga_buildings/11.vox", 16, "manor", "Large manor / hall"),
    ("castle", "models/castle.vox", 14, "castle", "MagicaVoxel sample castle"),
]


def read_magicavoxel_models(path: Path):
    data = path.read_bytes()
    if data[:4] != b"VOX ":
        raise ValueError(f"{path}: not a MagicaVoxel file")

    models = []
    palette = [(0, 0, 0)] + [(i, i, i) for i in range(1, 256)]
    cur_size = None

    def walk(start: int, end: int):
        nonlocal palette, cur_size
        i = start
        while i + 12 <= end:
            cid = data[i : i + 4]
            cs, ch = struct.unpack_from("<II", data, i + 4)
            body = data[i + 12 : i + 12 + cs]
            if cid == b"SIZE" and len(body) >= 12:
                cur_size = struct.unpack_from("<III", body[:12])
            elif cid == b"XYZI" and len(body) >= 4 and cur_size is not None:
                n = struct.unpack_from("<I", body[:4])[0]
                voxels = {}
                for j in range(n):
                    x, y, z, c = body[4 + j * 4 : 8 + j * 4]
                    if c:
                        voxels[(x, y, z)] = c
                models.append((cur_size, voxels))
                cur_size = None
            elif cid == b"RGBA" and len(body) >= 1024:
                palette = [(0, 0, 0)]
                for j in range(255):
                    r, g, b, _a = body[j * 4 : j * 4 + 4]
                    palette.append((r, g, b))
            if ch:
                walk(i + 12 + cs, i + 12 + cs + ch)
            i = i + 12 + cs + ch

    walk(8, len(data))
    if not models:
        raise ValueError(f"{path}: missing SIZE/XYZI")
    return models, palette


def occupied_bbox(voxels):
    xs = [p[0] for p in voxels]
    ys = [p[1] for p in voxels]
    zs = [p[2] for p in voxels]
    return (
        min(xs),
        min(ys),
        min(zs),
        max(xs) - min(xs) + 1,
        max(ys) - min(ys) + 1,
        max(zs) - min(zs) + 1,
    )


def pick_model(models):
    scored = []
    for idx, (_size, voxels) in enumerate(models):
        if not voxels:
            continue
        _bx, _by, _bz, sx, sy, sz = occupied_bbox(voxels)
        fill = len(voxels) / float(sx * sy * sz)
        if sx >= 100 and sz <= 20:
            continue
        if fill < 0.01 and sx >= 80:
            continue
        compact = 1.0 if sx <= 60 and sy <= 60 else 0.25
        height = 1.0 if sz >= 12 else 0.4
        mass = 1.0 if 400 <= len(voxels) <= 40000 else 0.5
        score = fill * compact * height * mass * len(voxels) ** 0.25
        scored.append((score, idx))
    if not scored:
        scored = [(len(v), i) for i, (_s, v) in enumerate(models) if v]
    scored.sort(reverse=True)
    return scored[0][1]


def downscale(voxels, factor: float, fill_threshold: float):
    if factor <= 1.0:
        return dict(voxels)
    buckets = {}
    for (x, y, z), value in voxels.items():
        key = (int(x / factor), int(y / factor), int(z / factor))
        buckets.setdefault(key, []).append(value)
    capacity = max(factor ** 3, 1.0)
    out = {}
    for key, values in buckets.items():
        if len(values) / capacity >= fill_threshold:
            out[key] = Counter(values).most_common(1)[0][0]
    return out


def normalise(voxels):
    min_x = min(p[0] for p in voxels)
    min_y = min(p[1] for p in voxels)
    min_z = min(p[2] for p in voxels)
    shifted = {(x - min_x, y - min_y, z - min_z): v for (x, y, z), v in voxels.items()}
    size = (
        max(p[0] for p in shifted) + 1,
        max(p[1] for p in shifted) + 1,
        max(p[2] for p in shifted) + 1,
    )
    return shifted, size


def map_rgb_to_voxel(r: int, g: int, b: int, building_type: str) -> int:
    """Map MagicaVoxel RGB onto VERSE construction materials."""
    mx = max(r, g, b)
    mn = min(r, g, b)
    sat = (mx - mn) / float(mx) if mx else 0.0

    # Near-black → skip (air) handled by caller if needed; treat as dark wood/soil.
    if mx < 28:
        return VT["WOOD_PINE"]

    # Glass / water-blue windows.
    if b > r + 25 and b > g + 10 and mx > 140:
        return VT["GLASS"]

    # Bright near-white → plaster walls.
    if mx > 200 and sat < 0.18:
        return VT["PLASTER"]

    # Red brick / terracotta roofs.
    if r > g + 40 and r > b + 40 and r > 100:
        if g < 90:
            return VT["BRICK"]
        return VT["TERRACOTTA"]

    # Yellow / gold thatch and straw roofs (prefer thatch for rural types).
    if r > 140 and g > 110 and b < 110 and r + g > 2 * b + 40:
        if building_type in ("hut", "cottage", "hall"):
            return VT["THATCH"]
        return VT["STRAW"] if sat > 0.35 else VT["THATCH"]

    # Grey stone / cobble.
    if sat < 0.12 and 70 <= mx <= 190:
        return VT["COBBLE"] if mx < 130 else VT["STONE"]

    # Warm browns → wood / plank / adobe.
    if r > g >= b and r > 60:
        if mx > 170 and sat < 0.35:
            return VT["PLANK"]
        if g > 90 and b > 50 and sat < 0.4:
            return VT["ADOBE"]
        if r > 120:
            return VT["WOOD_OAK"]
        return VT["WOOD_PINE"]

    # Green accents (shutters / moss) → pine wood so they stay structural.
    if g > r + 20 and g > b + 10:
        return VT["WOOD_PINE"]

    # Nearest construction colour fallback.
    best_t, best_d = VT["WOOD_OAK"], 1 << 30
    for t, pr, pg, pb in PALETTE:
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < best_d:
            best_d, best_t = d, t
    return best_t


def remap_voxels(voxels, palette, building_type: str, size):
    """Map colours, then bias the top storey toward roof materials."""
    _sx, _sy, sz = size
    roof_z0 = max(1, sz - max(2, (sz + 2) // 4))
    out = {}
    for (x, y, z), c in voxels.items():
        r, g, b = palette[c] if c < len(palette) else (128, 128, 128)
        t = map_rgb_to_voxel(r, g, b, building_type)
        if z >= roof_z0 and t not in (VT["GLASS"], VT["BRICK"], VT["STONE"], VT["COBBLE"]):
            if building_type in ("hut", "cottage", "hall"):
                t = VT["THATCH"]
            elif building_type in ("manor", "castle", "tower"):
                t = VT["TERRACOTTA"] if t != VT["WOOD_PINE"] else VT["WOOD_PINE"]
            else:
                t = VT["WOOD_PINE"]
        # Prefer timber framing over endless plaster for rural shells.
        if building_type in ("hut", "cottage") and t == VT["PLASTER"] and z < roof_z0:
            if (x + y) % 5 == 0:
                t = VT["WOOD_OAK"]
        out[(x, y, z)] = t
    return out


def write_vox(path: Path, size, voxels_typed, palette_rgb_by_type):
    sx, sy, sz = size
    used = sorted({v for v in voxels_typed.values()})
    remap = {src: i + 1 for i, src in enumerate(used)}

    size_chunk = struct.pack("<4sII", b"SIZE", 12, 0) + struct.pack("<III", sx, sy, sz)
    xyzi_body = struct.pack("<I", len(voxels_typed))
    for (x, y, z), value in voxels_typed.items():
        xyzi_body += bytes((x, y, z, remap[value]))
    xyzi_chunk = struct.pack("<4sII", b"XYZI", len(xyzi_body), 0) + xyzi_body

    rgba_body = b""
    for i in range(256):
        if i < len(used):
            r, g, b = palette_rgb_by_type[used[i]]
            rgba_body += bytes((r, g, b, 255))
        else:
            rgba_body += bytes((0, 0, 0, 255))
    rgba_chunk = struct.pack("<4sII", b"RGBA", 1024, 0) + rgba_body
    children = size_chunk + xyzi_chunk + rgba_chunk
    path.write_bytes(
        b"VOX "
        + struct.pack("<I", 150)
        + struct.pack("<4sII", b"MAIN", 0, len(children))
        + children
    )


def write_vbuild(path: Path, size, voxels_typed):
    """Compact settlement prefab: VBLD + dims + typed voxels."""
    sx, sy, sz = size
    body = bytearray()
    body += b"VBLD"
    body += struct.pack("<HHHH", 1, sx, sy, sz)  # ver, sx, sy, sz
    body += struct.pack("<I", len(voxels_typed))
    for (x, y, z), t in sorted(voxels_typed.items()):
        if max(x, y, z) > 255:
            raise ValueError("prefab axis exceeds u8")
        if t > 255:
            raise ValueError(f"VoxelType {t} exceeds u8")
        body += bytes((x, y, z, t))
    path.write_bytes(body)


def prepare_one(name, rel, footprint, building_type, note, force_footprint):
    src = ROOT / rel
    if not src.is_file():
        print(f"skip {name}: missing {rel}")
        return None

    models, palette = read_magicavoxel_models(src)
    idx = pick_model(models)
    _size, voxels = models[idx]
    if not voxels:
        print(f"skip {name}: empty")
        return None

    bx, by, bz, sx, sy, sz = occupied_bbox(voxels)
    voxels = {(x - bx, y - by, z - bz): c for (x, y, z), c in voxels.items()}

    target = force_footprint or footprint
    span = max(sx, sy)
    factor = max(span / float(target), 1.0)
    fill = len(voxels) / float(sx * sy * sz)
    threshold = 0.06 if fill < 0.05 else 0.16
    voxels = downscale(voxels, factor, threshold)
    if not voxels:
        print(f"skip {name}: empty after downscale")
        return None
    voxels, out_size = normalise(voxels)
    if out_size[2] < 3 or max(out_size[0], out_size[1]) > 24:
        print(f"skip {name}: bad size {out_size}")
        return None

    typed = remap_voxels(voxels, palette, building_type, out_size)
    type_rgb = {t: (r, g, b) for t, r, g, b in PALETTE}
    # Ensure every used type has an RGB (fallback grey).
    for t in typed.values():
        type_rgb.setdefault(t, (128, 128, 128))

    write_vox(OUT_DIR / f"{name}.vox", out_size, typed, type_rgb)
    write_vbuild(OUT_DIR / f"{name}.vbuild", out_size, typed)

    hist = Counter(typed.values())
    top = ", ".join(
        f"{k}:{v}"
        for k, v in hist.most_common(4)
    )
    print(f"{name} [{building_type}]: {out_size} {len(typed)}vox  ({top})  # {note}")
    return {
        "name": name,
        "type": building_type,
        "source": rel,
        "size": list(out_size),
        "voxels": len(typed),
        "note": note,
        "vox": f"models/buildings/{name}.vox",
        "vbuild": f"models/buildings/{name}.vbuild",
        "materials": {str(k): v for k, v in hist.items()},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--footprint", type=int, default=None)
    args = ap.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for old in list(OUT_DIR.glob("*.vox")) + list(OUT_DIR.glob("*.vbuild")):
        old.unlink()

    entries = []
    for name, rel, footprint, btype, note in CATALOG:
        info = prepare_one(name, rel, footprint, btype, note, args.footprint)
        if info:
            entries.append(info)

    (OUT_DIR / "manifest.json").write_text(json.dumps({"buildings": entries}, indent=2) + "\n")

    lines = [
        "# Game-scale settlement buildings",
        "",
        "Generated by `scripts/prepare_settlement_buildings.py` from CC0 MagicaVoxel",
        "houses (mehrasaur) plus the MagicaVoxel sample castle. Colours are remapped",
        "onto VERSE construction materials (wood, brick, thatch, plaster, …).",
        "",
        "Runtime stamps use `*.vbuild` (typed voxels). `*.vox` is for authoring.",
        "Home-drop (universe 0,0 scale 1) stays the procedural hunter's shack.",
        "",
        "| Prefab | Type | Size | Voxels | Role |",
        "|--------|------|------|--------|------|",
    ]
    for e in entries:
        sx, sy, sz = e["size"]
        lines.append(
            f"| `{e['name']}` | {e['type']} | {sx}×{sy}×{sz} | {e['voxels']} | {e['note']} |"
        )
    lines.extend(
        [
            "",
            "## Settlement scale mix",
            "",
            "| Scale | Types used |",
            "|-------|------------|",
            "| 1 Hut | `hut` (home-drop: procedural shack) |",
            "| 2–3 Hamlet | `hut`, `cottage` |",
            "| 4–5 Village | `cottage`, `house`, `shop`, `tower` + plaza |",
            "| 6–7 Town | + `hall`, `manor` + plaza / wall |",
            "| 8–9 City / Fortress | + `castle` landmark + denser mix |",
            "",
            "```bash",
            "python3 scripts/prepare_settlement_buildings.py",
            "```",
            "",
        ]
    )
    (OUT_DIR / "README.md").write_text("\n".join(lines))
    print(f"wrote {len(entries)} buildings -> {OUT_DIR}")


if __name__ == "__main__":
    main()
