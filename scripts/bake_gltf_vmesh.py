#!/usr/bin/env python3
"""Bake a skinned glTF into a Verse .vmesh (evaluated vertex frames).

Invoked by Blender:

    blender --background --python scripts/bake_gltf_vmesh.py -- \\
        models/poly/goleling.gltf models/poly/goleling.vmesh --height 1.8

The .vmesh is a packed little-endian blob the C loader in src/poly_mesh.c
reads: bind-pose colours, a shared index buffer, and per-clip xyz frames.
"""

import os
import struct
import sys


MAGIC = b"VMESH1\0\0"
CLIP_NAME_LEN = 32
MAX_TRIS = 1800
FPS = 12.0


def parse_args(argv):
    args = argv[:]
    height = 1.8
    if "--height" in args:
        i = args.index("--height")
        height = float(args[i + 1])
        del args[i:i + 2]
    if len(args) < 2:
        raise SystemExit("usage: bake_gltf_vmesh.py SRC.gltf DEST.vmesh [--height N]")
    return args[0], args[1], height


def blender_main(src, dest, target_height):
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
        bpy.ops.import_scene.gltf(filepath=src)

    mesh_objs = [o for o in bpy.data.objects if o.type == "MESH"]
    if not mesh_objs:
        raise RuntimeError(f"{src}: no mesh")

    # Prefer the densest mesh when leftovers (icospheres) are present, but keep
    # every mesh that contributes a meaningful fraction of the densest — Kenney
    # Cube Pets split body/legs/wings across objects that all animate together.
    def mesh_tris(obj):
        obj.data.calc_loop_triangles()
        return len(obj.data.loop_triangles)

    def has_armature_mod(obj):
        return any(m.type == "ARMATURE" for m in obj.modifiers)

    # Drop alternate skins / static duplicates common in Quaternius & OGA packs
    # (Frost_*, *_LP without _Anim, leftover Icospheres) when a skinned twin exists.
    skinned = [o for o in mesh_objs if has_armature_mod(o)]
    if skinned:
        prefer = []
        for o in skinned:
            n = o.name.lower()
            if "frost" in n or "preview" in n:
                continue
            prefer.append(o)
        if not prefer:
            prefer = skinned
        # Keep small companion meshes (Kenney legs/wings) that share the densest budget.
        densest_skin = max(mesh_tris(o) for o in prefer)
        companions = [
            o for o in mesh_objs
            if o not in prefer and mesh_tris(o) < densest_skin // 2
            and mesh_tris(o) >= 12 and "frost" not in o.name.lower()
            and "icosphere" not in o.name.lower() and "plane" not in o.name.lower()
        ]
        # Only keep companions when they look like a multi-part rig (several small pieces),
        # not a second full-body static copy of the same character.
        if len(companions) >= 2:
            mesh_objs = prefer + companions
        else:
            mesh_objs = prefer
        drop_all = [o for o in list(bpy.data.objects) if o.type == "MESH" and o not in mesh_objs]
        for extra in drop_all:
            bpy.data.objects.remove(extra, do_unlink=True)

    mesh_objs.sort(key=mesh_tris, reverse=True)
    densest = mesh_tris(mesh_objs[0])
    keep = [o for o in mesh_objs if mesh_tris(o) >= max(12, densest // 20)]
    # Always drop leftover icospheres / ground planes even if they pass the ratio test.
    keep = [o for o in keep
            if "icosphere" not in o.name.lower() and o.name.lower() != "plane"]
    drop = [o for o in mesh_objs if o not in keep]
    for extra in drop:
        bpy.data.objects.remove(extra, do_unlink=True)
    mesh_objs = keep
    if not mesh_objs:
        raise RuntimeError(f"{src}: no mesh left after filtering")
    print(f"  keeping meshes: {[o.name for o in mesh_objs]}")

    # Decimate each kept mesh independently so topology stays constant under the
    # armature / object animation that drives it afterwards.
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
            with bpy.context.temp_override(object=mesh_obj, active_object=mesh_obj,
                                           selected_objects=[mesh_obj]):
                bpy.ops.object.modifier_apply(modifier=mod.name)

    # Never transform_apply on skinned meshes — that breaks the bind pose. Scale / centre /
    # yaw are applied as a post-process on sampled xyz so parenting quirks (bats) cannot
    # leave the mesh at authoring scale.
    import math
    import re
    from bpy_extras.anim_utils import action_get_channelbag_for_slot

    def apply_action_frame(target_obj, action, frame):
        """Drive an object/armature to `frame`, forcing channelbag values when needed.

        Some Quaternius .blend files keep layered actions that do not advance via
        scene.frame_set alone; writing the channelbag into the pose fixes that.
        """
        if target_obj is None or action is None:
            return
        if not target_obj.animation_data:
            target_obj.animation_data_create()
        ad = target_obj.animation_data
        if ad.nla_tracks:
            for tr in ad.nla_tracks:
                tr.mute = True
            ad.use_nla = False
        ad.action = action
        if action.slots:
            try:
                ad.action_slot = action.slots[0]
            except Exception:
                pass
        bpy.context.scene.frame_set(int(round(frame)))
        slot = action.slots[0] if action.slots else None
        cb = action_get_channelbag_for_slot(action, slot) if slot else None
        if cb is None:
            bpy.context.view_layer.update()
            return
        bone_re = re.compile(r'^pose\.bones\["([^"]+)"\]\.(.+)$')
        for fcu in cb.fcurves:
            val = fcu.evaluate(frame)
            m = bone_re.match(fcu.data_path)
            if m and target_obj.type == "ARMATURE":
                bone = target_obj.pose.bones.get(m.group(1))
                if not bone:
                    continue
                attr = m.group(2)
                try:
                    cur = getattr(bone, attr)
                    if hasattr(cur, "__setitem__"):
                        cur[fcu.array_index] = val
                    else:
                        setattr(bone, attr, val)
                except Exception:
                    pass
                continue
            try:
                prop = target_obj.path_resolve(fcu.data_path)
                if hasattr(prop, "__setitem__"):
                    prop[fcu.array_index] = val
            except Exception:
                pass
        bpy.context.view_layer.update()

    # Stable mesh order so per-frame concatenation keeps the same vertex layout.
    mesh_objs.sort(key=lambda o: o.name)
    primary = mesh_objs[0]

    # Vertex colours sampled once on the bind pose, concatenated in mesh order.
    def sample_mesh_colors(mesh_obj):
        mesh_obj.data.calc_loop_triangles()
        uv_layer = mesh_obj.data.uv_layers.active
        image = None
        for mat in mesh_obj.data.materials:
            if not mat or not mat.node_tree:
                continue
            for node in mat.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image:
                    image = node.image
                    break
            if image:
                break
        pixels = list(image.pixels) if image else None
        iw = image.size[0] if image else 1
        ih = image.size[1] if image else 1
        nverts = len(mesh_obj.data.vertices)
        colors = bytearray(nverts * 3)

        def sample_rgb(u, v):
            if not pixels:
                return (180, 160, 120)
            x = int(max(0, min(iw - 1, u * iw)))
            y = int(max(0, min(ih - 1, v * ih)))
            i = (y * iw + x) * (len(pixels) // (iw * ih))
            r = int(max(0, min(255, pixels[i] * 255)))
            g = int(max(0, min(255, pixels[i + 1] * 255)))
            b = int(max(0, min(255, pixels[i + 2] * 255)))
            return (r, g, b)

        if uv_layer:
            for loop in mesh_obj.data.loops:
                u, v = uv_layer.data[loop.index].uv
                r, g, b = sample_rgb(u, v)
                colors[loop.vertex_index * 3:loop.vertex_index * 3 + 3] = bytes((r, g, b))
        else:
            colors[:] = bytes([160, 140, 110]) * nverts
        return colors

    colors = bytearray()
    indices = []
    vert_offsets = []
    nverts = 0
    for mesh_obj in mesh_objs:
        vert_offsets.append(nverts)
        colors.extend(sample_mesh_colors(mesh_obj))
        mesh_obj.data.calc_loop_triangles()
        for tri in mesh_obj.data.loop_triangles:
            indices.extend(v + nverts for v in tri.vertices)
        nverts += len(mesh_obj.data.vertices)
    nidx = len(indices)
    if nverts > 20000 or nidx > 60000:
        raise RuntimeError(f"{src}: too large after join ({nverts} verts, {nidx} indices)")

    armature = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
    actions = [a for a in bpy.data.actions
               if "preview" not in a.name.lower() and a.name != "SunAction"]
    if not actions:
        raise RuntimeError(f"{src}: no animation actions")

    # Prefer a short canonical subset when a pack ships dozens of clips.
    preferred = ("Idle", "Walk", "Run", "Death", "Jump", "static", "walk", "run",
                 "Flying_Idle", "Fast_Flying", "Bat_Idle", "Bat_Flying", "Bat_Die",
                 "Bat_Attack", "HitReact", "Punch", "Headbutt", "Yes", "No",
                 "movement", "Attack")
    ranked = []
    for name in preferred:
        for a in actions:
            short = a.name.split("|")[-1]
            if short == name or a.name.endswith(name) or short.endswith(name):
                if a not in ranked:
                    ranked.append(a)
    if ranked:
        actions = ranked

    # Gobkit free packs bake idle/attack/dead(/walk) into one "movement" timeline.
    # Slice that mega-action into named clips the runtime already understands.
    # Ranges are source frames at the pack's authored fps (usually 24).
    gobkit_slices = (
        ("Idle", 0, 29),
        ("Attack", 30, 59),
        ("Death", 60, 89),
        ("Walk", 90, 119),
    )

    scene = bpy.context.scene
    scene.render.fps = int(FPS)
    clips = []
    work_list = []  # (clip_name, action, start_frame, end_frame)
    for action in actions:
        name = action.name.split("|")[-1][:CLIP_NAME_LEN - 1]
        # Strip a leading "Bat_" and normalize Die -> Death for the shared runtime.
        if name.startswith("Bat_"):
            name = name[4:]
        if name == "Die":
            name = "Death"
        if name == "static":
            name = "Idle"
        if name == "walk":
            name = "Walk"
        if name == "run":
            name = "Run"
        frames = sorted({kp.co.x for fcu in action.fcurves for kp in fcu.keyframe_points})
        lo = frames[0] if frames else 0.0
        hi = frames[-1] if frames else scene.render.fps
        if name.lower() == "movement" and hi >= 60.0:
            for clip_name, a, b in gobkit_slices:
                if a > hi:
                    continue
                work_list.append((clip_name, action, float(a), float(min(b, hi))))
            continue
        work_list.append((name, action, float(lo), float(hi)))

    for name, action, start, end in work_list:
        action_paths = {fcu.data_path for fcu in action.fcurves}
        bone_only = any(p.startswith("pose.bones") for p in action_paths) and not any(
            not p.startswith("pose.bones") for p in action_paths)
        driven = []
        if armature and bone_only:
            driven.append(armature)
        else:
            # Kenney Cube Pets: channels live on empties / mesh parts.
            for obj in list(bpy.data.objects):
                if obj.type in ("EMPTY", "ARMATURE", "MESH"):
                    driven.append(obj)

        duration = max((end - start) / scene.render.fps, 1.0 / FPS)
        frame_count = max(2, int(round(duration * FPS)))
        xyz = []
        for i in range(frame_count):
            t = start + (end - start) * (i / (frame_count - 1))
            for obj in driven:
                apply_action_frame(obj, action, t)
            frame_xyz = []
            for mesh_obj in mesh_objs:
                has_arm = any(m.type == "ARMATURE" for m in mesh_obj.modifiers)
                if has_arm:
                    # OGA Vampire Bat (and similar .blends) do not deform through the
                    # depsgraph evaluated mesh; applying the Armature modifier does.
                    dup = mesh_obj.copy()
                    dup.data = mesh_obj.data.copy()
                    bpy.context.collection.objects.link(dup)
                    bpy.ops.object.select_all(action="DESELECT")
                    dup.select_set(True)
                    bpy.context.view_layer.objects.active = dup
                    for mod in list(dup.modifiers):
                        if mod.type in ("ARMATURE", "DECIMATE"):
                            try:
                                with bpy.context.temp_override(
                                        object=dup, active_object=dup,
                                        selected_objects=[dup]):
                                    bpy.ops.object.modifier_apply(modifier=mod.name)
                            except Exception:
                                pass
                    mw = dup.matrix_world
                    expected = len(mesh_obj.data.vertices)
                    if len(dup.data.vertices) != expected:
                        n = len(dup.data.vertices)
                        mesh_data = dup.data
                        bpy.data.objects.remove(dup, do_unlink=True)
                        bpy.data.meshes.remove(mesh_data)
                        raise RuntimeError(
                            f"{name}/{mesh_obj.name}: vertex count changed "
                            f"({n} vs {expected})")
                    for v in dup.data.vertices:
                        pt = mw @ v.co
                        frame_xyz.extend((pt.x, pt.y, pt.z))
                    mesh_data = dup.data
                    bpy.data.objects.remove(dup, do_unlink=True)
                    bpy.data.meshes.remove(mesh_data)
                else:
                    deps = bpy.context.evaluated_depsgraph_get()
                    eval_obj = mesh_obj.evaluated_get(deps)
                    eval_mesh = eval_obj.to_mesh()
                    mw = eval_obj.matrix_world
                    expected = len(mesh_obj.data.vertices)
                    if len(eval_mesh.vertices) != expected:
                        eval_obj.to_mesh_clear()
                        raise RuntimeError(
                            f"{name}/{mesh_obj.name}: vertex count changed "
                            f"({len(eval_mesh.vertices)} vs {expected})")
                    for v in eval_mesh.vertices:
                        pt = mw @ v.co
                        frame_xyz.extend((pt.x, pt.y, pt.z))
                    eval_obj.to_mesh_clear()
            if len(frame_xyz) != nverts * 3:
                raise RuntimeError(f"{name}: concatenated vertex mismatch")
            xyz.extend(frame_xyz)
        # Drop static Idle placeholders (Kenney "static") so AI falls through to Walk.
        unique = 1
        for fi in range(1, frame_count):
            base0 = 0
            base1 = fi * nverts * 3
            delta = 0.0
            for k in range(nverts * 3):
                delta += abs(xyz[base1 + k] - xyz[base0 + k])
            if delta / nverts > 1e-4:
                unique += 1
                break
        if name == "Idle" and unique < 2 and any(
                a.name.split("|")[-1] in ("Walk", "walk", "Flying_Idle") for a in actions):
            print(f"  skip  {name:16s} (static placeholder; AI will use Walk)")
            continue
        clips.append((name, frame_count, duration, xyz))
        print(f"  clip {name:16s} frames={frame_count:3d} duration={duration:.2f}s unique~={unique}")

    # Post-process: yaw -90°, scale to target height (with wingspan cap), sole on z=0.
    def aabb_of(xyz):
        xs = xyz[0:nverts * 3:3]
        ys = xyz[1:nverts * 3:3]
        zs = xyz[2:nverts * 3:3]
        return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))

    rest0 = next((c for c in clips if c[0] in ("Idle", "Flying_Idle")), clips[0])
    minx, maxx, miny, maxy, minz, maxz = aabb_of(rest0[3])
    size_x, size_y, size_z = maxx - minx, maxy - miny, maxz - minz
    scale = target_height / max(size_z, 1e-4)
    max_xy = max(size_x, size_y, 1e-4)
    wing_cap = target_height * 5.0
    if max_xy * scale > wing_cap:
        scale = wing_cap / max_xy
        print(f"  wing-cap scale -> {scale:.4f} (xy {max_xy:.3f} -> {wing_cap:.3f})")
    # Pivot from unscaled rest: footprint centre, sole.
    cx, cy = 0.5 * (minx + maxx), 0.5 * (miny + maxy)
    cos_a, sin_a = 0.0, -1.0  # -90° around Z
    new_clips = []
    for name, fc, dur, xyz in clips:
        out = []
        for i in range(0, len(xyz), 3):
            x, y, z = xyz[i] - cx, xyz[i + 1] - cy, xyz[i + 2] - minz
            x, y = (x * cos_a - y * sin_a), (x * sin_a + y * cos_a)
            out.extend((x * scale, y * scale, z * scale))
        new_clips.append((name, fc, dur, out))
    clips = new_clips
    rest_n = next((c for c in clips if c[0] in ("Idle", "Flying_Idle")), clips[0])
    minx, maxx, miny, maxy, minz, maxz = aabb_of(rest_n[3])
    print(f"  normalized aabb xy=({maxx-minx:.3f},{maxy-miny:.3f}) "
          f"z=[{minz:.3f},{maxz:.3f}] scale={scale:.4f}")

    # Guard against bind-pose breakage: a locomotion clip whose first-frame diagonal is
    # several times the rest pose almost always means transform_apply ran on a skinned mesh.
    def frame0_diag(xyz):
        xs = xyz[0:nverts * 3:3]
        ys = xyz[1:nverts * 3:3]
        zs = xyz[2:nverts * 3:3]
        dx, dy, dz = max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)
        return (dx * dx + dy * dy + dz * dz) ** 0.5

    rest = next((c for c in clips if c[0] in ("Idle", "Flying_Idle", "static")), clips[0])
    rest_diag = frame0_diag(rest[3])
    for name, _fc, _dur, xyz in clips:
        if name in ("Death", "Die", "HitReact"):
            continue
        diag = frame0_diag(xyz)
        print(f"  aabb-diag {name:16s} {diag:.3f} (rest {rest_diag:.3f})")
        if rest_diag > 1e-4 and diag > rest_diag * 2.5:
            raise RuntimeError(
                f"{src}: clip {name!r} first-frame diagonal {diag:.3f} is "
                f">{diag / rest_diag:.1f}x rest {rest_diag:.3f} — skinned bind pose likely broken")


    # Locomotion must actually move vertices — stale/broken bakes shipped as T-poses before.
    def clip_moves(xyz, frame_count):
        if frame_count < 2:
            return False
        mid = (frame_count // 2) * nverts * 3
        delta = 0.0
        for k in range(nverts * 3):
            delta += abs(xyz[mid + k] - xyz[k])
        return (delta / nverts) > 1e-3

    loco = [c for c in clips if c[0] in ("Walk", "Run", "Flying", "Fast_Flying")]
    if loco and not any(clip_moves(c[3], c[1]) for c in loco):
        raise RuntimeError(
            f"{src}: locomotion clips have no vertex motion — armature not evaluated")

    zs = rest[3][2:nverts * 3:3]
    foot = min(zs) if zs else 0.0
    if abs(foot) > 0.25:
        print(f"  warn: rest sole z={foot:.3f} (expected near 0)")

    os.makedirs(os.path.dirname(os.path.abspath(dest)) or ".", exist_ok=True)
    with open(dest, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<IIf", nverts, nidx, target_height))
        f.write(struct.pack("<I", len(clips)))
        f.write(bytes(colors))
        f.write(struct.pack("<" + "H" * nidx, *indices))
        for name, frame_count, duration, xyz in clips:
            raw = name.encode("ascii", "replace")[:CLIP_NAME_LEN - 1]
            f.write(raw + b"\0" * (CLIP_NAME_LEN - len(raw)))
            f.write(struct.pack("<IfI", frame_count, duration, 1))
            f.write(struct.pack("<" + "f" * len(xyz), *xyz))

    size = os.path.getsize(dest)
    print(f"{src} -> {dest}")
    print(f"  verts={nverts} tris={nidx // 3} clips={len(clips)} bytes={size}")


def main():
    # Blender puts its own flags before "--".
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    src, dest, height = parse_args(argv)
    blender_main(src, dest, height)


if __name__ == "__main__":
    main()
