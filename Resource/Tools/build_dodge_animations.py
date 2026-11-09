"""Bake and stage the CQ-05B action bank without rewriting prior clips."""
import ast
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Tools"))
from civic_motion import Pose, blend
from dodge_motion import (DURATION, FPS, FRAMES, LAND, LAND_END, PUSH_END,
                          REVISION, SPECS, direction, pose_for, reference, travel)

STAGE = ROOT / "Validation/Dodge_Staging"
MASTER = ROOT / "Source/Erma_Resource_Library.blend"
CIVIC = ROOT / "Source/Civic_Animations.blend"


def load_functions(file, *names):
    tree = ast.parse((ROOT / "Tools" / file).read_text(encoding="utf-8"))
    nodes = [node for node in tree.body
             if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names]
    exec(compile(ast.Module(body=nodes, type_ignores=[]), file, "exec"), globals())


def read(file):
    return json.loads((ROOT / file).read_text(encoding="utf-8"))


def write(file, data):
    (ROOT / file).write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n",
                             encoding="utf-8")


def action_signature(action):
    curves = [
        (fc.data_path, fc.array_index, fc.extrapolation,
         [(tuple(k.co), tuple(k.handle_left), tuple(k.handle_right), k.interpolation)
          for k in fc.keyframe_points])
        for fc in action.fcurves
    ]
    return hashlib.sha256(json.dumps(curves).encode()).hexdigest()


def existing_signatures():
    return {a.name: action_signature(a) for a in bpy.data.actions
            if a.name not in {"HUMAN_7H__" + name for name in SPECS}}


def bind_pose(rig):
    return [(b.name, b.parent.name if b.parent else None, [list(row) for row in b.matrix_local])
            for b in rig.data.bones]


def sampler(scene, rig):
    cache = {}

    def sample(name, phase):
        key = (name, phase)
        if key not in cache:
            action = bpy.data.actions["HUMAN_7H__" + name]
            bind(scene, rig, action, phase)
            evaluated = rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
            pose = Pose(rig)
            pose.basis = {b.name: b.matrix_basis.copy() for b in evaluated.pose.bones}
            cache[key] = pose
        return cache[key]

    return sample


def bake(rig, name, poses):
    action = bpy.data.actions.get(name) or bpy.data.actions.new(name)
    action.fcurves.clear()
    action.use_fake_user = True
    action["motion_revision"] = REVISION
    action["loop"] = False
    rig.animation_data.action = action
    rig.animation_data.use_nla = False
    if action.slots:
        rig.animation_data.action_slot = action.slots[0]
    previous = {}
    for index, pose in enumerate(poses):
        pose.apply()
        for bone in rig.pose.bones:
            rotation = bone.rotation_quaternion
            if bone.name in previous and rotation.dot(previous[bone.name]) < 0:
                rotation.negate()
            previous[bone.name] = rotation.copy()
            bone.keyframe_insert("rotation_quaternion", frame=index / 2)
            bone.keyframe_insert("location", frame=index / 2)
    for curve in action.fcurves:
        for key in curve.keyframe_points:
            key.interpolation = "LINEAR"
    return action


def attach_tracks(rig, muted=True, scale=1):
    rig.animation_data_create()
    for track in list(rig.animation_data.nla_tracks):
        if track.name in SPECS:
            rig.animation_data.nla_tracks.remove(track)
    for name in SPECS:
        track = rig.animation_data.nla_tracks.new()
        track.name = name
        strip = track.strips.new(name, 0, bpy.data.actions["HUMAN_7H__" + name])
        strip.name = name
        strip.scale = scale
        track.mute = muted


def inspect_contacts(name, push, poses):
    heading = direction(name)
    land = "R" if push == "L" else "L"
    report = {}
    for label, foot, first, last in [("push", push, 0, round(PUSH_END * 60)),
                                      ("land", land, round(LAND * 60), round(LAND_END * 60))]:
        anchors = [poses[i].fk("foot." + foot).translation + heading * travel(i / 60)
                   for i in range(first, last + 1)]
        drift = max((anchor - anchors[0]).length for anchor in anchors)
        report[label] = {"foot": foot, "max_anchor_drift_m": drift}
    return report


def asset_context():
    global catalog, recipes, PROTOTYPES, MATS
    catalog = read("Documentation/asset_catalog.json")
    recipes = read("Documentation/assembly_recipes.json")
    PROTOTYPES = {a["id"]: bpy.data.objects.get(a.get("blender_object", a["id"]))
                  for a in catalog["assets"]}
    MATS = {material.name: material for material in bpy.data.materials}


def author():
    global human
    bpy.ops.wm.open_mainfile(filepath=str(MASTER))
    entry = bpy.context.scene.name
    human = bpy.data.objects["HUMAN_7H"]
    asset_context()
    load_functions("build_civic_animations.py", "Clearance", "geometry_signature")
    before = existing_signatures()
    geometry = geometry_signature()
    bones = bind_pose(human)
    assert len(bones) == 23
    scene = bpy.data.scenes.new("Dodge_Authoring")
    scene.render.fps = FPS
    scene.collection.objects.link(human)
    bpy.context.window.scene = scene
    sample = sampler(scene, human)
    clearance = Clearance()
    stats = {}
    for clip, (name, push) in SPECS.items():
        poses = [pose_for(name, push, i / 60, sample) for i in range(2 * FRAMES + 1)]
        for pose in poses[1:-1]:
            clearance.correct(pose)
        low = min(clearance.minimum(pose) for pose in poses)
        assert low >= -.005, (clip, low)
        assert all(pose.basis["root"].is_identity for pose in poses), clip
        contacts = inspect_contacts(name, push, poses)
        assert max(row["max_anchor_drift_m"] for row in contacts.values()) < .015, (clip, contacts)
        stats[clip] = {"min_skin_z_m": low, "contacts": contacts}
        bake(human, "HUMAN_7H__" + clip, poses)
        print("DODGE_BAKED", clip, json.dumps(stats[clip]), flush=True)
    assert before == existing_signatures()
    assert geometry == geometry_signature() and bones == bind_pose(human)
    human.animation_data.action = None
    human.animation_data.use_nla = False
    Pose(human).apply()
    bpy.context.window.scene = bpy.data.scenes[entry]
    bpy.data.scenes.remove(scene)
    STAGE.mkdir(parents=True, exist_ok=True)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(STAGE / "Dodge_Draft.blend"), compress=True)
    write("Validation/dodge_authoring.json", {
        "clips": stats, "old_actions_preserved": len(before), "bones": len(bones),
        "geometry_preserved": True, "entry_scene": entry,
        "master_sha256": hashlib.sha256(MASTER.read_bytes()).hexdigest(),
    })


if __name__ == "__main__":
    load_functions("verify_civic_animations.py", "bind")
    author()
