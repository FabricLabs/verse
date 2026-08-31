#!/usr/bin/env python3
"""Voxelize a mesh into MagicaVoxel .vox via Blender's Voxel Remesh.

    blender --background --python scripts/voxelize_mesh_to_vox.py -- \\
        SRC.obj models/sheep.vox --height 24 --voxel-size 0.04
"""

import os
import struct
import sys
from collections import Counter


def parse_args(argv):
    args = argv[:]
    height = 24
    voxel_size = 0.04
    if "--height" in args:
        i = args.index("--height")
        height = int(args[i + 1])
        del args[i:i + 2]
    if "--voxel-size" in args:
        i = args.index("--voxel-size")
        voxel_size = float(args[i + 1])
        del args[i:i + 2]
    if len(args) < 2:
        raise SystemExit(
            "usage: voxelize_mesh_to_vox.py SRC DEST.vox [--height N] [--voxel-size F]"
        )
    return args[0], args[1], height, voxel_size


def write_vox(path, size, voxels, palette):
    sx, sy, sz = size
    used = sorted({v for v in voxels.values()})
    remap = {src: i + 1 for i, src in enumerate(used)}
    size_chunk = struct.pack("<4sII", b"SIZE", 12, 0) + struct.pack("<III", sx, sy, sz)
    xyzi_body = struct.pack("<I", len(voxels))
    for (x, y, z), value in voxels.items():
        xyzi_body += bytes((x, y, z, remap[value]))
    xyzi_chunk = struct.pack("<4sII", b"XYZI", len(xyzi_body), 0) + xyzi_body
    rgba_body = b""
    for i in range(256):
        if i < len(used):
            r, g, b = palette[used[i]]
            rgba_body += bytes((r, g, b, 255))
        else:
            rgba_body += bytes((0, 0, 0, 255))
    rgba_chunk = struct.pack("<4sII", b"RGBA", 1024, 0) + rgba_body
    children = size_chunk + xyzi_chunk + rgba_chunk
    open(path, "wb").write(
        b"VOX " + struct.pack("<I", 150)
        + struct.pack("<4sII", b"MAIN", 0, len(children)) + children
    )


def blender_main(src, dest, target_height, voxel_size):
    import bpy
    from mathutils import Vector

    bpy.ops.wm.read_factory_settings(use_empty=True)
    ext = os.path.splitext(src)[1].lower()
    if ext == ".obj":
        bpy.ops.wm.obj_import(filepath=src)
    elif ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=src)
    elif ext in (".gltf", ".glb"):
        bpy.ops.import_scene.gltf(filepath=src)
    elif ext == ".blend":
        bpy.ops.wm.open_mainfile(filepath=src)
    else:
        raise RuntimeError(f"unsupported source: {src}")

    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    if not meshes:
        raise RuntimeError(f"{src}: no mesh")
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    mesh_obj = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    remesh = mesh_obj.modifiers.new("vox_remesh", "REMESH")
    remesh.mode = "VOXEL"
    remesh.voxel_size = voxel_size
    remesh.adaptivity = 0.0
    with bpy.context.temp_override(object=mesh_obj, active_object=mesh_obj,
                                   selected_objects=[mesh_obj]):
        bpy.ops.object.modifier_apply(modifier=remesh.name)

    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    eval_obj = mesh_obj.evaluated_get(deps)
    eval_mesh = eval_obj.to_mesh()
    mw = eval_obj.matrix_world

    # Round every remeshed vertex into an integer cell.
    cells = set()
    for v in eval_mesh.vertices:
        p = mw @ v.co
        cells.add((int(round(p.x / voxel_size)),
                   int(round(p.y / voxel_size)),
                   int(round(p.z / voxel_size))))
    eval_obj.to_mesh_clear()
    if not cells:
        raise RuntimeError(f"{src}: remesh produced no cells")

    min_x = min(c[0] for c in cells)
    min_y = min(c[1] for c in cells)
    min_z = min(c[2] for c in cells)
    shifted = {(x - min_x, y - min_y, z - min_z) for (x, y, z) in cells}
    max_z = max(z for (_, _, z) in shifted)
    cur_h = max_z + 1
    factor = cur_h / float(target_height) if target_height > 0 else 1.0
    if factor > 1.01:
        buckets = {}
        for (x, y, z) in shifted:
            key = (int(x / factor), int(y / factor), int(z / factor))
            buckets.setdefault(key, 0)
            buckets[key] += 1
        # Keep cells that got at least one sample.
        shifted = set(buckets.keys())
        min_x = min(c[0] for c in shifted)
        min_y = min(c[1] for c in shifted)
        min_z = min(c[2] for c in shifted)
        shifted = {(x - min_x, y - min_y, z - min_z) for (x, y, z) in shifted}
        max_z = max(z for (_, _, z) in shifted)

    palette = {
        1: (52, 48, 44),
        2: (90, 84, 76),
        3: (170, 166, 156),
        4: (220, 216, 204),
        5: (240, 236, 224),
        6: (40, 36, 34),
        7: (200, 140, 120),
    }
    voxels = {}
    max_x = max(x for (x, _, _) in shifted)
    for (x, y, z) in shifted:
        t = z / max(max_z, 1)
        if t < 0.18:
            colour = 6
        elif t < 0.35:
            colour = 2
        elif t < 0.75:
            colour = 4
        else:
            colour = 5
        if x > max_x * 0.72 and 0.45 < t < 0.7:
            colour = 7
        voxels[(x, y, z)] = colour

    size_out = (
        max(p[0] for p in voxels) + 1,
        max(p[1] for p in voxels) + 1,
        max(p[2] for p in voxels) + 1,
    )
    os.makedirs(os.path.dirname(os.path.abspath(dest)) or ".", exist_ok=True)
    write_vox(dest, size_out, voxels, palette)
    print(f"{src} -> {dest} {size_out}, {len(voxels)} voxels")


def main():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    src, dest, height, voxel_size = parse_args(argv)
    blender_main(src, dest, height, voxel_size)


if __name__ == "__main__":
    main()
