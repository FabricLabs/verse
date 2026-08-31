#!/usr/bin/env python3
"""Convert OpenGameArt "voxel creatures" raw grids into MagicaVoxel .vox for models/.

The source files use a bespoke layout rather than the MagicaVoxel chunk format:

    u32 dx, u32 dy, u32 dz
    dx*dy*dz palette-index bytes, 0xFF meaning empty, stored z-fastest
    256 * 3 bytes of 6-bit RGB palette

Vertical axis runs downwards in the source, so it is flipped to the Z-up
convention that MagicaVoxel and models/ use. Each source file also carries a
strip of loose voxels acting as a colour legend; keeping only the largest
connected component drops it without touching the creature.

Usage:
    import_oga_vox.py SRC.vox DEST.vox --height 32 [--palette mud]
"""

import argparse
import struct
from collections import Counter, deque


def read_source(path):
    data = open(path, 'rb').read()
    dx, dy, dz = struct.unpack('<III', data[:12])
    count = dx * dy * dz
    body = data[12:12 + count]
    raw_palette = data[12 + count:12 + count + 768]
    if len(body) != count or len(raw_palette) != 768:
        raise ValueError(f'{path}: unexpected size for {dx}x{dy}x{dz}')

    # 6-bit VGA-style channels widened to 8-bit.
    palette = [(raw_palette[i * 3] * 255 // 63,
                raw_palette[i * 3 + 1] * 255 // 63,
                raw_palette[i * 3 + 2] * 255 // 63) for i in range(256)]

    voxels = {}
    for i, value in enumerate(body):
        if value == 0xFF:
            continue
        z = i % dz
        y = (i // dz) % dy
        x = i // (dz * dy)
        voxels[(x, y, dz - 1 - z)] = value
    return (dx, dy, dz), voxels, palette


def largest_component(voxels):
    """Keep the biggest 6-connected blob, discarding the baked-in colour legend."""
    seen = set()
    best = []
    for start in voxels:
        if start in seen:
            continue
        blob = []
        queue = deque([start])
        seen.add(start)
        while queue:
            x, y, z = queue.popleft()
            blob.append((x, y, z))
            for dx, dy, dz in ((1, 0, 0), (-1, 0, 0), (0, 1, 0),
                               (0, -1, 0), (0, 0, 1), (0, 0, -1)):
                n = (x + dx, y + dy, z + dz)
                if n in voxels and n not in seen:
                    seen.add(n)
                    queue.append(n)
        if len(blob) > len(best):
            best = blob
    return {p: voxels[p] for p in best}


def downscale(voxels, factor, fill_threshold=0.22):
    """Box-filter to 1/factor scale, taking the majority colour of each cell."""
    if factor <= 1.0:
        return voxels
    buckets = {}
    for (x, y, z), value in voxels.items():
        key = (int(x / factor), int(y / factor), int(z / factor))
        buckets.setdefault(key, []).append(value)
    capacity = factor ** 3
    out = {}
    for key, values in buckets.items():
        if len(values) / capacity >= fill_threshold:
            out[key] = Counter(values).most_common(1)[0][0]
    return out


def normalise(voxels):
    """Shift to the origin so the model sits in a tight box."""
    min_x = min(p[0] for p in voxels)
    min_y = min(p[1] for p in voxels)
    min_z = min(p[2] for p in voxels)
    shifted = {(x - min_x, y - min_y, z - min_z): v for (x, y, z), v in voxels.items()}
    size = (max(p[0] for p in shifted) + 1,
            max(p[1] for p in shifted) + 1,
            max(p[2] for p in shifted) + 1)
    return shifted, size


def mud_palette(palette, used):
    """Restyle a lava palette as wet clay.

    Source luminance is bunched at the dark end, so colours are ranked among
    those the model actually uses and spread evenly across an earth-to-tan
    ramp. That keeps the glowing cracks readable as wet highlights.
    """
    ramp = [(38, 26, 17), (58, 40, 26), (82, 58, 38), (104, 76, 50),
            (126, 95, 63), (146, 113, 78), (166, 133, 96), (188, 158, 120)]
    lum = {i: (palette[i][0] * 299 + palette[i][1] * 587 + palette[i][2] * 114) // 1000
           for i in used}
    order = sorted(used, key=lambda i: lum[i])
    out = list(palette)
    span = max(len(order) - 1, 1)
    for rank, index in enumerate(order):
        out[index] = ramp[rank * (len(ramp) - 1) // span]
    return out


def write_vox(path, size, voxels, palette):
    """Emit a single-model MagicaVoxel .vox (version 150)."""
    sx, sy, sz = size
    if max(size) > 255:
        raise ValueError(f'{size} exceeds the 255-per-axis .vox limit')

    # .vox palette entry N is stored at index N-1, and index 0 means empty.
    used = sorted({v for v in voxels.values()})
    remap = {src: i + 1 for i, src in enumerate(used)}
    if len(remap) > 255:
        raise ValueError('more than 255 distinct colours')

    size_chunk = struct.pack('<4sII', b'SIZE', 12, 0) + struct.pack('<III', sx, sy, sz)

    xyzi_body = struct.pack('<I', len(voxels))
    for (x, y, z), value in voxels.items():
        xyzi_body += bytes((x, y, z, remap[value]))
    xyzi_chunk = struct.pack('<4sII', b'XYZI', len(xyzi_body), 0) + xyzi_body

    rgba_body = b''
    for i in range(256):
        if i < len(used):
            r, g, b = palette[used[i]]
            rgba_body += bytes((r, g, b, 255))
        else:
            rgba_body += bytes((0, 0, 0, 255))
    rgba_chunk = struct.pack('<4sII', b'RGBA', 1024, 0) + rgba_body

    children = size_chunk + xyzi_chunk + rgba_chunk
    out = (b'VOX ' + struct.pack('<I', 150)
           + struct.pack('<4sII', b'MAIN', 0, len(children)) + children)
    open(path, 'wb').write(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source')
    ap.add_argument('dest')
    ap.add_argument('--height', type=int, default=32,
                    help='target height in voxels; the model scales to match')
    ap.add_argument('--palette', choices=('source', 'mud'), default='source')
    args = ap.parse_args()

    dims, voxels, palette = read_source(args.source)
    voxels = largest_component(voxels)

    height = max(p[2] for p in voxels) - min(p[2] for p in voxels) + 1
    voxels = downscale(voxels, height / args.height)
    voxels, size = normalise(voxels)

    if args.palette == 'mud':
        palette = mud_palette(palette, sorted(set(voxels.values())))

    write_vox(args.dest, size, voxels, palette)
    print(f'{args.source} {dims} -> {args.dest} {size}, {len(voxels)} voxels')


if __name__ == '__main__':
    main()
