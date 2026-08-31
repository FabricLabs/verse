#!/usr/bin/env python3
"""Build a procedural bone+flesh voxel humanoid around a CC0 skinned skeleton.

Takes Kenney Animated Characters 3 (characterMedium.fbx + idle/run/jump) or any
rigged FBX/glTF with an armature, then:

  1. Merges locomotion clips onto the bind mesh
  2. Rasterizes every bone as a VOXEL_BONE capsule and a thicker VOXEL_FLESH shell
  3. Writes MagicaVoxel .vox (palette colours match voxel.h BONE/FLESH)
  4. Bakes evaluated vertex frames to .vmesh (and a provenance glTF)

    blender --background --python scripts/skeleton_voxel_flesh.py -- \\
      models/cc0_skeleton/kenney_animated_characters_3 \\
      models/flesh_walker.vox models/poly/flesh_walker.vmesh --height 56 --mesh-height 1.85
"""

from __future__ import annotations

import math
import os
import struct
import sys


# Exact gameplay colours from voxel.h (VOXEL_COLOR_BONE / VOXEL_COLOR_FLESH).
BONE_RGB = (0xF0, 0xF0, 0xDC)
FLESH_RGB = (0xFF, 0xC8, 0xC8)

# MagicaVoxel palette indices (1-based in XYZI).
PAL_BONE = 1
PAL_FLESH = 2

VMESH_MAGIC = b"VMESH1\0\0"
CLIP_NAME_LEN = 32
MAX_TRIS = 1800
FPS = 12.0


def parse_args(argv):
    args = argv[:]
    height = 56
    mesh_height = 1.85
    if "--height" in args:
        i = args.index("--height")
        height = int(args[i + 1])
        del args[i : i + 2]
    if "--mesh-height" in args:
        i = args.index("--mesh-height")
        mesh_height = float(args[i + 1])
        del args[i : i + 2]
    if len(args) < 3:
        raise SystemExit(
            "usage: skeleton_voxel_flesh.py SRC_DIR DEST.vox DEST.vmesh "
            "[--height N] [--mesh-height F]"
        )
    return args[0], args[1], args[2], height, mesh_height


def write_vox(path, size, voxels):
    sx, sy, sz = size
    size_chunk = struct.pack("<4sII", b"SIZE", 12, 0) + struct.pack("<III", sx, sy, sz)
    xyzi_body = struct.pack("<I", len(voxels))
    for (x, y, z), value in voxels.items():
        xyzi_body += bytes((x, y, z, value))
    xyzi_chunk = struct.pack("<4sII", b"XYZI", len(xyzi_body), 0) + xyzi_body
    rgba_body = b""
    palette = {PAL_BONE: BONE_RGB, PAL_FLESH: FLESH_RGB}
    for i in range(256):
        if i + 1 in palette:
            r, g, b = palette[i + 1]
            rgba_body += bytes((r, g, b, 255))
        else:
            rgba_body += bytes((0, 0, 0, 255))
    rgba_chunk = struct.pack("<4sII", b"RGBA", 1024, 0) + rgba_body
    children = size_chunk + xyzi_chunk + rgba_chunk
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    open(path, "wb").write(
        b"VOX "
        + struct.pack("<I", 150)
        + struct.pack("<4sII", b"MAIN", 0, len(children))
        + children
    )


def bone_radii(name: str):
    """Return (bone_core_radius, flesh_shell_radius) in armature-local units."""
    n = name.lower()
    if any(s in n for s in ("head", "neck")):
        return 0.045, 0.13
    if any(s in n for s in ("spine", "hip", "pelvis", "torso", "chest", "abdomen")):
        return 0.04, 0.16
    if any(s in n for s in ("upperarm", "arm", "shoulder", "clavicle")):
        return 0.03, 0.09
    if any(s in n for s in ("forearm", "lowerarm", "elbow", "ulna", "radius")):
        return 0.025, 0.075
    if any(s in n for s in ("hand", "wrist", "finger", "thumb")):
        return 0.015, 0.04
    if any(s in n for s in ("upleg", "thigh", "upperleg", "leg")):
        return 0.035, 0.11
    if any(s in n for s in ("leg", "shin", "calf", "lowerleg", "knee")):
        return 0.03, 0.09
    if any(s in n for s in ("foot", "toe", "ankle")):
        return 0.02, 0.055
    return 0.02, 0.07


def skip_bone(name: str) -> bool:
    n = name.lower()
    if n.startswith("ik") or "ik_" in n:
        return True
    if any(s in n for s in ("target", "pole", "ctrl", "helper", "root_motion", "nub")):
        return True
    return False


def stamp_capsule(voxels, a, b, radius, value, cell):
    """Fill a capsule from a→b (world space) into the integer voxel dict."""
    ax, ay, az = a
    bx, by, bz = b
    dx, dy, dz = bx - ax, by - ay, bz - az
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 1e-6:
        samples = [(ax, ay, az)]
    else:
        steps = max(2, int(math.ceil(length / (cell * 0.55))))
        samples = [
            (ax + dx * (i / (steps - 1)), ay + dy * (i / (steps - 1)), az + dz * (i / (steps - 1)))
            for i in range(steps)
        ]
    r_cells = max(1, int(math.ceil(radius / cell)))
    r2 = (radius + cell * 0.35) ** 2
    for sx, sy, sz in samples:
        cx, cy, cz = int(round(sx / cell)), int(round(sy / cell)), int(round(sz / cell))
        for oz in range(-r_cells, r_cells + 1):
            for oy in range(-r_cells, r_cells + 1):
                for ox in range(-r_cells, r_cells + 1):
                    px = (cx + ox) * cell
                    py = (cy + oy) * cell
                    pz = (cz + oz) * cell
                    if length < 1e-6:
                        d2 = (px - ax) ** 2 + (py - ay) ** 2 + (pz - az) ** 2
                    else:
                        t = ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / (length * length)
                        t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
                        qx = ax + dx * t
                        qy = ay + dy * t
                        qz = az + dz * t
                        d2 = (px - qx) ** 2 + (py - qy) ** 2 + (pz - qz) ** 2
                    if d2 <= r2:
                        key = (cx + ox, cy + oy, cz + oz)
                        if value == PAL_BONE or key not in voxels:
                            voxels[key] = value


def bake_vmesh(armature, mesh_objs, dest, target_height):
    """Bake skinned clips to .vmesh, matching scripts/bake_gltf_vmesh.py layout."""
    import bpy
    from mathutils import Vector

    mesh_objs = sorted(mesh_objs, key=lambda o: o.name)

    for mesh_obj in mesh_objs:
        bpy.ops.object.mode_set(mode="OBJECT")
        bpy.ops.object.select_all(action="DESELECT")
        mesh_obj.select_set(True)
        bpy.context.view_layer.objects.active = mesh_obj
        mesh_obj.data.calc_loop_triangles()
        tris = len(mesh_obj.data.loop_triangles)
        per_budget = max(200, MAX_TRIS // max(len(mesh_objs), 1))
        if tris > per_budget:
            mod = mesh_obj.modifiers.new("vmesh_decimate", "DECIMATE")
            mod.ratio = per_budget / float(tris)
            while mesh_obj.modifiers[0] != mod:
                bpy.ops.object.modifier_move_up(modifier=mod.name)
            with bpy.context.temp_override(
                object=mesh_obj, active_object=mesh_obj, selected_objects=[mesh_obj]
            ):
                bpy.ops.object.modifier_apply(modifier=mod.name)

    bpy.context.view_layer.update()
    bbox = []
    for mesh_obj in mesh_objs:
        bbox.extend(mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box)
    min_c = Vector((min(v.x for v in bbox), min(v.y for v in bbox), min(v.z for v in bbox)))
    max_c = Vector((max(v.x for v in bbox), max(v.y for v in bbox), max(v.z for v in bbox)))
    height = max(max_c.z - min_c.z, 1e-4)
    scale = target_height / height
    for mesh_obj in mesh_objs:
        mesh_obj.scale = (
            mesh_obj.scale[0] * scale,
            mesh_obj.scale[1] * scale,
            mesh_obj.scale[2] * scale,
        )
    bpy.ops.object.select_all(action="DESELECT")
    for mesh_obj in mesh_objs:
        mesh_obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_objs[0]
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    # Kenney faces +Z; Verse wants +X forward.
    for mesh_obj in mesh_objs:
        mesh_obj.rotation_euler[2] += -1.57079632679
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    bpy.context.view_layer.update()
    bbox = []
    for mesh_obj in mesh_objs:
        bbox.extend(mesh_obj.matrix_world @ Vector(c) for c in mesh_obj.bound_box)
    min_c = Vector((min(v.x for v in bbox), min(v.y for v in bbox), min(v.z for v in bbox)))
    max_c = Vector((max(v.x for v in bbox), max(v.y for v in bbox), max(v.z for v in bbox)))
    pivot = Vector((0.5 * (min_c.x + max_c.x), 0.5 * (min_c.y + max_c.y), min_c.z))
    for mesh_obj in mesh_objs:
        mesh_obj.location -= pivot
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)

    colors = bytearray()
    indices = []
    nverts = 0
    for mesh_obj in mesh_objs:
        mesh_obj.data.calc_loop_triangles()
        n = len(mesh_obj.data.vertices)
        attr = mesh_obj.data.color_attributes.get("BoneFlesh")
        per_vert = bytearray([FLESH_RGB[0], FLESH_RGB[1], FLESH_RGB[2]] * n)
        if attr is not None:
            for loop in mesh_obj.data.loops:
                c = attr.data[loop.index].color
                vi = loop.vertex_index
                per_vert[vi * 3 : vi * 3 + 3] = bytes(
                    (int(c[0] * 255), int(c[1] * 255), int(c[2] * 255))
                )
        colors.extend(per_vert)
        for tri in mesh_obj.data.loop_triangles:
            indices.extend(v + nverts for v in tri.vertices)
        nverts += n
    nidx = len(indices)

    actions = [a for a in bpy.data.actions if a.name in ("Idle", "Walk", "Run", "Jump")]
    if not actions:
        actions = list(bpy.data.actions)
    if not actions:
        raise RuntimeError("no animation actions to bake")

    scene = bpy.context.scene
    scene.render.fps = int(FPS)
    clips = []
    for action in actions:
        name = action.name[: CLIP_NAME_LEN - 1]
        if not armature.animation_data:
            armature.animation_data_create()
        armature.animation_data.action = action
        frames = sorted({kp.co.x for fcu in action.fcurves for kp in fcu.keyframe_points})
        if len(frames) < 2:
            start, end = 1.0, scene.render.fps
        else:
            start, end = frames[0], frames[-1]
        duration = max((end - start) / scene.render.fps, 1.0 / FPS)
        frame_count = max(2, int(round(duration * FPS)))
        xyz = []
        for i in range(frame_count):
            t = start + (end - start) * (i / (frame_count - 1))
            scene.frame_set(int(round(t)))
            bpy.context.view_layer.update()
            deps = bpy.context.evaluated_depsgraph_get()
            frame_xyz = []
            for mesh_obj in mesh_objs:
                eval_obj = mesh_obj.evaluated_get(deps)
                eval_mesh = eval_obj.to_mesh()
                mw = eval_obj.matrix_world
                expected = len(mesh_obj.data.vertices)
                if len(eval_mesh.vertices) != expected:
                    eval_obj.to_mesh_clear()
                    raise RuntimeError(
                        f"{name}/{mesh_obj.name}: vertex count changed "
                        f"({len(eval_mesh.vertices)} vs {expected})"
                    )
                for v in eval_mesh.vertices:
                    p = mw @ v.co
                    frame_xyz.extend((p.x, p.y, p.z))
                eval_obj.to_mesh_clear()
            xyz.extend(frame_xyz)
        clips.append((name, frame_count, duration, xyz))
        print(f"  clip {name:16s} frames={frame_count:3d} duration={duration:.2f}s")

    os.makedirs(os.path.dirname(os.path.abspath(dest)) or ".", exist_ok=True)
    with open(dest, "wb") as f:
        f.write(VMESH_MAGIC)
        f.write(struct.pack("<IIf", nverts, nidx, target_height))
        f.write(struct.pack("<I", len(clips)))
        f.write(bytes(colors))
        f.write(struct.pack("<" + "H" * nidx, *indices))
        for name, frame_count, duration, xyz in clips:
            raw = name.encode("ascii", "replace")[: CLIP_NAME_LEN - 1]
            f.write(raw + b"\0" * (CLIP_NAME_LEN - len(raw)))
            f.write(struct.pack("<IfI", frame_count, duration, 1))
            f.write(struct.pack("<" + "f" * len(xyz), *xyz))
    print(f"vmesh {dest}: verts={nverts} tris={nidx // 3} clips={len(clips)}")


def blender_main(src_dir, dest_vox, dest_vmesh, target_height, mesh_height):
    import bpy

    bpy.ops.wm.read_factory_settings(use_empty=True)

    model = os.path.join(src_dir, "Model", "characterMedium.fbx")
    if not os.path.isfile(model):
        model = src_dir
    if not os.path.isfile(model):
        raise RuntimeError(f"missing model: {model}")

    bpy.ops.import_scene.fbx(filepath=model, automatic_bone_orientation=True)

    armature = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    if not armature:
        raise RuntimeError(f"{model}: no armature")
    if not meshes:
        raise RuntimeError(f"{model}: no mesh")

    skin = os.path.join(src_dir, "Skins", "humanMaleA.png")
    if os.path.isfile(skin):
        img = bpy.data.images.load(skin)
        for mesh_obj in meshes:
            if not mesh_obj.data.materials:
                mat = bpy.data.materials.new("FleshSkin")
                mat.use_nodes = True
                mesh_obj.data.materials.append(mat)
            for mat in mesh_obj.data.materials:
                if not mat or not mat.use_nodes:
                    continue
                nt = mat.node_tree
                tex = nt.nodes.new("ShaderNodeTexImage")
                tex.image = img
                bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
                if bsdf:
                    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])

    anim_dir = os.path.join(src_dir, "Animations")
    clip_files = {
        "Idle": "idle.fbx",
        "Run": "run.fbx",
        "Jump": "jump.fbx",
    }
    if os.path.isdir(anim_dir):
        for action in list(bpy.data.actions):
            bpy.data.actions.remove(action)
        for clip_name, fname in clip_files.items():
            path = os.path.join(anim_dir, fname)
            if not os.path.isfile(path):
                continue
            before = set(bpy.data.actions)
            bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=True)
            # Drop the animation FBX's duplicate armature/mesh; keep its actions.
            for obj in list(bpy.data.objects):
                if obj != armature and obj not in meshes:
                    if obj.type in ("ARMATURE", "MESH", "EMPTY"):
                        bpy.data.objects.remove(obj, do_unlink=True)
            new_actions = [a for a in bpy.data.actions if a not in before]
            if not new_actions:
                continue
            # Kenney packs ship a 2-frame "Targeting Pose" plus the real clip —
            # keep the action whose name contains the clip (Idle/Run/Jump).
            needle = clip_name.lower()
            ranked = [
                a
                for a in new_actions
                if needle in a.name.lower() and "targeting" not in a.name.lower()
            ]
            keeper = ranked[0] if ranked else max(
                new_actions, key=lambda a: abs(a.frame_range[1] - a.frame_range[0])
            )
            keeper.name = clip_name
            for extra in new_actions:
                if extra != keeper:
                    bpy.data.actions.remove(extra)
        run = bpy.data.actions.get("Run")
        if run and "Walk" not in bpy.data.actions:
            walk = run.copy()
            walk.name = "Walk"

    if armature.animation_data:
        armature.animation_data.action = None
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode="POSE")
    bpy.ops.pose.select_all(action="SELECT")
    bpy.ops.pose.transforms_clear()
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()

    segments = []
    for bone in armature.pose.bones:
        if skip_bone(bone.name):
            continue
        mw = armature.matrix_world
        head = mw @ bone.head
        tail = mw @ bone.tail
        length = (tail - head).length
        if length < 1e-4:
            continue
        core, shell = bone_radii(bone.name)
        segments.append((bone.name, head.copy(), tail.copy(), core, shell))

    if not segments:
        raise RuntimeError("no usable bones for voxel flesh")

    min_z = min(min(h.z, t.z) for _, h, t, _, _ in segments)
    max_z = max(max(h.z, t.z) for _, h, t, _, _ in segments)
    max_shell = max(s for *_, s in segments)
    span = max((max_z + max_shell) - (min_z - max_shell), 1e-4)
    cell = span / float(target_height)

    voxels = {}
    for _name, head, tail, core, shell in segments:
        stamp_capsule(voxels, head, tail, shell, PAL_FLESH, cell)
    for _name, head, tail, core, shell in segments:
        stamp_capsule(voxels, head, tail, core, PAL_BONE, cell)

    if not voxels:
        raise RuntimeError("voxelisation produced nothing")

    min_x = min(p[0] for p in voxels)
    min_y = min(p[1] for p in voxels)
    min_z_i = min(p[2] for p in voxels)
    shifted = {
        (x - min_x, y - min_y, z - min_z_i): v for (x, y, z), v in voxels.items()
    }
    size_out = (
        max(p[0] for p in shifted) + 1,
        max(p[1] for p in shifted) + 1,
        max(p[2] for p in shifted) + 1,
    )
    write_vox(dest_vox, size_out, shifted)
    bone_n = sum(1 for v in shifted.values() if v == PAL_BONE)
    flesh_n = sum(1 for v in shifted.values() if v == PAL_FLESH)
    print(
        f"voxel {dest_vox}: {size_out[0]}x{size_out[1]}x{size_out[2]} "
        f"bone={bone_n} flesh={flesh_n} bones={len(segments)} cell={cell:.4f}"
    )

    bpy.context.view_layer.update()
    for mesh_obj in meshes:
        mesh = mesh_obj.data
        if not mesh.color_attributes:
            mesh.color_attributes.new(name="BoneFlesh", type="BYTE_COLOR", domain="CORNER")
        attr = mesh.color_attributes.get("BoneFlesh") or mesh.color_attributes[0]
        mw = mesh_obj.matrix_world
        for li, loop in enumerate(mesh.loops):
            p = mw @ mesh.vertices[loop.vertex_index].co
            best = 1e9
            best_core = 0.02
            for _n, head, tail, core, shell in segments:
                d = (tail - head).length
                if d < 1e-6:
                    dist = (p - head).length
                else:
                    t = max(0.0, min(1.0, ((p - head).dot(tail - head)) / (d * d)))
                    dist = (p - (head + (tail - head) * t)).length
                if dist < best:
                    best = dist
                    best_core = core
            if best <= best_core * 1.35:
                r, g, b = BONE_RGB
            else:
                r, g, b = FLESH_RGB
            attr.data[li].color = (r / 255.0, g / 255.0, b / 255.0, 1.0)

        mat = mesh_obj.data.materials[0] if mesh_obj.data.materials else None
        if mat is None:
            mat = bpy.data.materials.new("BoneFleshMat")
            mat.use_nodes = True
            mesh_obj.data.materials.append(mat)
        mat.use_nodes = True
        nt = mat.node_tree
        for n in list(nt.nodes):
            nt.nodes.remove(n)
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        vcol = nt.nodes.new("ShaderNodeVertexColor")
        vcol.layer_name = attr.name
        nt.links.new(vcol.outputs["Color"], bsdf.inputs["Base Color"])
        nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    bake_vmesh(armature, meshes, dest_vmesh, mesh_height)

    dest_gltf = os.path.splitext(dest_vmesh)[0] + ".gltf"
    if not armature.animation_data:
        armature.animation_data_create()
    while armature.animation_data.nla_tracks:
        armature.animation_data.nla_tracks.remove(armature.animation_data.nla_tracks[0])
    for action in bpy.data.actions:
        track = armature.animation_data.nla_tracks.new()
        track.name = action.name
        start = int(action.frame_range[0])
        track.strips.new(action.name, start, action)
    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    for mesh_obj in meshes:
        mesh_obj.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.gltf(
        filepath=dest_gltf,
        export_format="GLTF_SEPARATE",
        use_selection=True,
        export_animations=True,
        export_animation_mode="NLA_TRACKS",
        export_nla_strips=True,
        export_apply=False,
        export_skins=True,
        export_morph=False,
    )
    print(f"gltf {dest_gltf}: actions={[a.name for a in bpy.data.actions]}")


def main():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    src, dest_vox, dest_vmesh, height, mesh_height = parse_args(argv)
    blender_main(src, dest_vox, dest_vmesh, height, mesh_height)


if __name__ == "__main__":
    main()
