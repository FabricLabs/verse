#!/usr/bin/env python3
"""Summarise the models/ collection as JSON.

Groups the assets by stem, then reports geometry, animation frames and material
composition for each. Reads .vox chunks directly and the .world text format
written by world_serialize().

Usage:
    model_inventory.py [--models-dir models] [--voxel-header src/voxel.h] [--out -]
"""

import argparse
import json
import os
import re
import struct
from collections import Counter

VOX_EXT = '.vox'
WORLD_EXT = '.world'
MESH_EXTS = ('.obj', '.fbx', '.blend', '.stl', '.dae', '.gltf', '.glb')
TEXTURE_EXTS = ('.png', '.mtl', '.jpg', '.jpeg', '.bmp')


def voxel_type_names(header_path):
    """Pull the canonical VoxelType enum out of voxel.h, in declaration order."""
    text = open(header_path, encoding='utf-8', errors='replace').read()
    start = text.index('VOXEL_AIR = 0')
    end = text.index('VOXEL_COUNT', start)
    body = re.sub(r'/\*.*?\*/', '', text[start:end], flags=re.S)
    body = re.sub(r'//[^\n]*', '', body)

    names = {}
    value = 0
    for entry in body.split(','):
        entry = entry.strip()
        if not entry:
            continue
        m = re.match(r'^(VOXEL_[A-Z0-9_]+)\s*(?:=\s*(\d+))?$', entry)
        if not m:
            continue
        if m.group(2) is not None:
            value = int(m.group(2))
        names[value] = m.group(1)
        value += 1
    return names


def read_vox(path):
    """Frame sizes and per-frame voxel counts from a MagicaVoxel .vox file."""
    data = open(path, 'rb').read()
    if data[:4] != b'VOX ':
        return None

    version = struct.unpack('<I', data[4:8])[0]
    sizes, counts, colors = [], [], None
    i = 8
    while i + 12 <= len(data):
        chunk_id = data[i:i + 4]
        content_size = struct.unpack('<I', data[i + 4:i + 8])[0]
        body = data[i + 12:i + 12 + content_size]
        if chunk_id == b'SIZE':
            sizes.append(struct.unpack('<III', body[:12]))
        elif chunk_id == b'XYZI' and len(body) >= 4:
            n = struct.unpack('<I', body[:4])[0]
            counts.append(n)
            palette_indices = {body[4 + j * 4 + 3] for j in range(n)}
            colors = len(palette_indices) if colors is None else colors
        # MAIN holds everything else as children rather than content.
        i += 12 if chunk_id == b'MAIN' else 12 + content_size

    if not sizes:
        return None
    return {
        'version': version,
        'frames': len(sizes),
        'size': list(sizes[0]),
        'voxels': counts[0] if counts else 0,
        'total_voxels': sum(counts),
        'distinct_colors': colors or 0,
    }


def read_world(path, type_names):
    """Dimensions and material histogram from a .world snapshot.

    Header is fixed-width hex: version(4) w(8) h(8) d(8) log_length(8). The
    per-voxel stream that follows is one hex digit each and cannot express
    types above 15, so the authoritative types come from the VOX2 section.
    """
    text = open(path, encoding='utf-8', errors='replace').read()
    if len(text) < 36:
        return None
    try:
        version = int(text[0:4], 16)
        width = int(text[4:12], 16)
        height = int(text[12:20], 16)
        depth = int(text[20:28], 16)
        log_length = int(text[28:36], 16)
    except ValueError:
        return None

    total = width * height * depth
    counts = Counter()

    marker = text.find('\nVOX2\n')
    if marker != -1:
        m = re.search(r'\nt (\d+)\n([^\n]*)', text[marker:])
        if m:
            for run in m.group(2).split():
                length, _, type_id = run.partition(':')
                if type_id:
                    counts[int(type_id)] += int(length)

    if not counts:
        stream = text[36 + log_length:36 + log_length + total]
        for ch, n in Counter(stream).items():
            if ch in '0123456789ABCDEFabcdef':
                counts[int(ch, 16)] += n

    solid = sum(n for t, n in counts.items() if t != 0)
    materials = [{'type': t, 'name': type_names.get(t, f'TYPE_{t}'), 'count': n}
                 for t, n in counts.most_common() if t != 0]

    meta = {}
    meta_at = text.find('\nMETA1\n')
    if meta_at != -1:
        for line in text[meta_at + 7:meta_at + 512].split('\n'):
            key, _, value = line.partition('=')
            if key in ('gen', 'grav', 'seed') and value:
                meta[key] = value.strip()

    return {
        'version': version,
        'size': [width, height, depth],
        'cells': total,
        'solid': solid,
        'fill': round(solid / total, 5) if total else 0.0,
        'materials': materials[:6],
        'distinct_materials': len(materials),
        'meta': meta,
    }


def categorise(rel_path, stem):
    lowered = rel_path.lower()
    if stem.startswith('layer_'):
        return 'terrain layer'
    if '/anim' in lowered or lowered.startswith('anim'):
        return 'animated'
    if stem.startswith('vds_'):
        return 'dungeon set'
    if any(k in lowered for k in ('ui', 'gore ui')):
        return 'interface'
    if any(k in lowered for k in ('monu', 'castle', 'room', 'doom', 'wall', 'roof', 'tile')):
        return 'structure'
    if any(k in lowered for k in ('knight', 'golem', 'bird', 'deer', 'rex', 'snake', 'lizard',
                                  'croc', 'human', 'skeleton', 'zombie', 'spider', 'slime')):
        return 'creature'
    return 'prop'


def build(models_dir, type_names):
    assets = {}
    for root, _dirs, files in os.walk(models_dir):
        for filename in files:
            path = os.path.join(root, filename)
            rel = os.path.relpath(path, models_dir)
            stem, ext = os.path.splitext(filename)
            ext = ext.lower()
            if stem.endswith('.preview'):
                stem = stem[:-len('.preview')]

            key = os.path.join(os.path.dirname(rel), stem) if os.path.dirname(rel) else stem
            entry = assets.setdefault(key, {
                'name': stem,
                'path': os.path.dirname(rel) or '.',
                'category': categorise(rel, stem),
                'files': [],
                'bytes': 0,
            })
            entry['files'].append(ext)
            entry['bytes'] += os.path.getsize(path)

            try:
                if ext == VOX_EXT:
                    info = read_vox(path)
                    if info:
                        entry['vox'] = info
                elif ext == WORLD_EXT:
                    info = read_world(path, type_names)
                    if info:
                        entry['world'] = info
            except (OSError, ValueError, struct.error):
                continue

    out = []
    for key, entry in sorted(assets.items()):
        entry['formats'] = sorted(set(entry.pop('files')))
        entry['has_mesh'] = any(e in MESH_EXTS for e in entry['formats'])
        entry['has_texture'] = any(e in TEXTURE_EXTS for e in entry['formats'])
        entry['animated'] = bool(entry.get('vox', {}).get('frames', 1) > 1)
        entry['key'] = key
        out.append(entry)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--models-dir', default='models')
    ap.add_argument('--voxel-header', default='src/voxel.h')
    ap.add_argument('--out', default='-')
    args = ap.parse_args()

    type_names = voxel_type_names(args.voxel_header)
    data = build(args.models_dir, type_names)
    payload = json.dumps(data, indent=1)
    if args.out == '-':
        print(payload)
    else:
        open(args.out, 'w').write(payload)
        print(f'{len(data)} assets -> {args.out}')


if __name__ == '__main__':
    main()
