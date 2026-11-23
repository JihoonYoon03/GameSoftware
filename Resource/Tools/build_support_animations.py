"""CQ-07C2: bake support actions and one shared export bank on HUMAN_7H."""
import math
from pathlib import Path
import sys
import time

import bpy
from mathutils import Matrix, Vector
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_dodge_animations as existing
from civic_motion import Pose
from support_motion import CASE_BOTTOM, REVISION, SPECS, pose_for, upper_body_error
from texture_pilot import ROOT, digest, write_json

BANK = ROOT / "Export/AnimationBanks/HUMAN_7H_Support.glb"
SCENES = ("47_Support_Treatment", "48_Support_Terminal", "49_Support_Carry")
PREVIEW = ROOT / "Preview/Support"


def bind_action(actor, action):
    actor.animation_data_create()
    actor.animation_data.action = action
    actor.animation_data.action_slot = action.slots[0]
    actor.animation_data.use_nla = False


def add_actor(scene_collection, rig, recipe, label, location=(0,0,0), yaw=0.0):
    data = existing.recipes[recipe]
    parts = [p for p in data["parts"] if p != "Weapon_CoilPistol_35cm"]
    actor, _ = existing.clone_actor(label, rig, parts, scene_collection, location,
                                   data.get("skin"), data.get("offsets"))
    actor.rotation_euler.z = yaw
    return actor


def prop(name, col, location=(0,0,0), yaw=0.0):
    obj = existing.PROTOTYPES[name].copy()
    obj.name = "SupportReview_" + name
    obj.parent = None
    obj.modifiers.clear()
    obj.location = location
    obj.rotation_euler.z = yaw
    col.objects.link(obj)
    return obj


def build_review(rig, poses, sample, render):
    PREVIEW.mkdir(parents=True, exist_ok=True)
    records = {}
    for scene_name, center, mode in zip(SCENES, ((0,-.45,.58),(0,-.42,.85),(0,-.18,.85)),
                                        ("Treat", "Inspect", "Carry")):
        camera = (-2.9,-1.15,2.07) if mode == "Inspect" else (2.9,-2.75,2.1)
        scene, col = existing.studio(scene_name, camera, center, 2.65, (700,620))
        existing.ground(col, 7, -.008)
        bpy.context.window.scene = scene
        actor = add_actor(col, rig, "Ria_Engineer", scene_name[:2]+"_Ria")
        loop = "Support_" + mode + ("_Walk" if mode == "Carry" else "_Loop")
        action = bpy.data.actions["HUMAN_7H__"+loop]
        bind_action(actor, action)
        scene.frame_start = 0
        scene.frame_end = SPECS[loop][0]
        if mode == "Treat":
            patient_pose = sample("Downed_Back_Loop", 0)
            yaw = math.pi/2
            rotation = Matrix.Rotation(yaw, 4, "Z")
            patient_chest = patient_pose.fk("chest").translation
            chest_xy = rotation @ patient_chest
            patient_origin = Vector((.0,-.69,0)) - Vector((chest_xy.x,chest_xy.y,0))
            patient = add_actor(col, rig, "Player_Explorer", "47_Patient", patient_origin, yaw)
            bind_action(patient, bpy.data.actions["HUMAN_7H__Downed_Back_Loop"])
            prop("Interior_Med_Kit", col, (.47,-.34,0), -.13)
            records["treatment_patient_actor_offset_m"] = list(patient_origin)
            records["treatment_patient_actor_yaw_deg"] = 90
        elif mode == "Inspect":
            prop("Interior_Terminal", col, (0,-.84,0), math.pi)
        else:
            case = prop("Interior_Med_Kit", col)
            case.rotation_mode = "QUATERNION"
            for i, pose in enumerate(poses[loop]):
                case.matrix_world = pose.deformation("chest") @ Matrix.Translation(CASE_BOTTOM)
                case.keyframe_insert("location", frame=i/2)
                case.keyframe_insert("rotation_quaternion", frame=i/2)
            for curve in case.animation_data.action.fcurves:
                for key in curve.keyframe_points:
                    key.interpolation = "LINEAR"
        scene.frame_set(round(scene.frame_end*.48))
        if render:
            scene.cycles.samples = 16
            scene.render.filepath = str(PREVIEW / (mode + ".png"))
            bpy.ops.render.render(write_still=True)
    return records


def export_bank(rig, path=BANK, clips=SPECS):
    path.parent.mkdir(parents=True, exist_ok=True)
    scene = bpy.data.scenes.new("Support_Bank_Export")
    scene.render.fps = 60
    bpy.context.window.scene = scene
    bank_rig = rig.copy()
    bank_rig.data = rig.data.copy()
    scene.collection.objects.link(bank_rig)
    bank_rig.name = path.stem
    bank_rig.animation_data_clear()
    bank_rig.animation_data_create()
    Pose(bank_rig).apply()
    for name in clips:
        track = bank_rig.animation_data.nla_tracks.new()
        track.name = name
        strip = track.strips.new(name, 0, bpy.data.actions["HUMAN_7H__"+name])
        strip.name = name
        strip.scale = 2
    anchor = existing.PROTOTYPES["Body_Hand_L"].copy()
    scene.collection.objects.link(anchor)
    anchor.name = "SupportBank_BindReference"
    anchor.parent = bank_rig
    anchor["attachment_role"] = "bind_reference_not_rendered"
    for modifier in anchor.modifiers:
        if modifier.type == "ARMATURE":
            modifier.object = bank_rig
    for obj in scene.objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = bank_rig
    scene.frame_set(0)
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True,
        use_active_scene=True, export_animations=True, export_animation_mode="NLA_TRACKS",
        export_force_sampling=True, export_frame_range=False, export_frame_step=1,
        export_skins=True, export_def_bones=False, export_yup=True,
        export_anim_slide_to_zero=True, export_optimize_animation_size=True, export_extras=True)
    bpy.data.objects.remove(anchor, do_unlink=True)
    armature = bank_rig.data
    bpy.data.objects.remove(bank_rig, do_unlink=True)
    bpy.data.armatures.remove(armature)
    return scene


def save_checked(path, initial_hash):
    temporary = path.with_suffix(".support-publish.blend")
    bpy.ops.wm.save_as_mainfile(filepath=str(temporary), compress=True)
    assert digest(path) == initial_hash, f"{path.name} changed during authoring"
    for attempt in range(10):
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if attempt == 9:
                raise
            time.sleep(.3 * (attempt+1))


def author():
    master_hash, civic_hash = digest(existing.MASTER), digest(existing.CIVIC)
    bpy.ops.wm.open_mainfile(filepath=str(existing.MASTER))
    clear_review_scenes()
    entry = bpy.context.scene.name
    rig = bpy.data.objects["HUMAN_7H"]
    existing.human = rig
    existing.asset_context()
    existing.load_functions("build_civic_animations.py", "Clearance", "geometry_signature")
    existing.load_functions("verify_civic_animations.py", "bind")
    existing.load_functions("build_resource_pack.py", "clone_actor", "studio", "ground")
    action_names = {"HUMAN_7H__"+name for name in SPECS}
    before = {a.name: existing.action_signature(a) for a in bpy.data.actions if a.name not in action_names}
    geometry_before = existing.geometry_signature()
    bone_table = existing.bind_pose(rig)
    source_scene = bpy.data.scenes.new("Support_Authoring")
    source_scene.collection.objects.link(rig)
    source_scene.render.fps = 30
    bpy.context.window.scene = source_scene
    sample = existing.sampler(source_scene, rig)
    floor = existing.Clearance()
    poses, stats = {}, {}
    for clip, (frames, loop, layer) in SPECS.items():
        poses[clip] = rows = [pose_for(clip, i/(frames*2), sample) for i in range(frames*2+1)]
        if layer == "full_body":
            # Correct the knees and boots on fitted actor geometry.
            for pose in rows:
                if not all(pose.basis[n] == sample("Idle",0).basis[n] for n in pose.basis):
                    floor.correct(pose)
        if loop:
            rows[-1] = rows[0]
        for pose in rows:
            assert pose.basis["root"].is_identity, clip
        minimum = min(floor.minimum(p) for p in rows)
        assert minimum >= -.005, (clip, minimum)
        if layer != "full_body":
            base = "Walk" if clip.endswith("_Walk") else "Idle"
            use_phase = clip.startswith("Support_Carry")
            error = max(upper_body_error(sample(base, i/(frames*2) if use_phase and i < frames*2 else 0), p)
                        for i,p in enumerate(rows))
            assert error < 2e-5, (clip, error)
        else:
            error = None
        action = existing.bake(rig, "HUMAN_7H__"+clip, rows)
        action["motion_revision"] = REVISION
        action["loop"] = loop
        stats[clip] = {"duration_seconds": frames/30, "loop": loop, "layer": layer,
                       "min_fitted_skin_z_m": minimum, "lower_body_max_error": error}
        print("SUPPORT_BAKED", clip, stats[clip], flush=True)

    def pose_error(a,b):
        return max(abs(a.basis[n][i][j]-b.basis[n][i][j]) for n in a.basis
                   for i in range(4) for j in range(4))

    for mode in ("Treat","Inspect"):
        enter, repeat, leave = [poses[f"Support_{mode}_{section}"] for section in ("Enter","Loop","Exit")]
        assert pose_error(enter[-1],repeat[0]) < 2e-5 and pose_error(repeat[-1],leave[0]) < 2e-5
    assert before == {name: existing.action_signature(bpy.data.actions[name]) for name in before}
    assert geometry_before == existing.geometry_signature() and bone_table == existing.bind_pose(rig)
    patient_pose = sample("Downed_Back_Loop",0)
    review = build_review(rig, poses, lambda n,t: patient_pose, "--render" in sys.argv)
    bank_scene = export_bank(rig)
    bpy.context.window.scene = bpy.data.scenes[entry]
    bpy.data.scenes.remove(bank_scene)
    bpy.data.scenes.remove(source_scene)
    rig.animation_data.action = None
    Pose(rig).apply()
    bpy.context.preferences.filepaths.save_version = 0
    save_checked(existing.MASTER, master_hash)

    bpy.ops.wm.open_mainfile(filepath=str(existing.CIVIC))
    clear_review_scenes()
    civic_entry = bpy.context.scene.name
    prior = {a.name: existing.action_signature(a) for a in bpy.data.actions if a.name not in action_names}
    for name in action_names:
        if action := bpy.data.actions.get(name):
            bpy.data.actions.remove(action, do_unlink=True)
    with bpy.data.libraries.load(str(existing.MASTER), link=False) as (data, output):
        assert set(SCENES) <= set(data.scenes)
        output.actions = sorted(action_names)
        output.scenes = list(SCENES)
    for name in action_names:
        bpy.data.actions[name].use_fake_user = True
    assert prior == {name: existing.action_signature(bpy.data.actions[name]) for name in prior}
    assert bpy.context.scene.name == civic_entry
    bpy.context.preferences.filepaths.save_version = 0
    save_checked(existing.CIVIC, civic_hash)
    reference = poses["Support_Carry_Idle"][0]
    socket = reference.fk("hand.R").inverted() @ reference.deformation("chest") @ Matrix.Translation(CASE_BOTTOM)
    write_json(ROOT/"Validation/support_animation_authoring.json", {
        "original_actions_preserved": len(before), "original_civic_actions_preserved": len(prior),
        "skeleton_bones": len(bone_table), "bank_sha256": digest(BANK), "clips": stats,
        "carry_case_hand_R_local_matrix": [list(row) for row in socket], **review,
        "all_checks_passed": True,
    })
    print("SUPPORT_AUTHORED", len(SPECS), flush=True)


def clear_review_scenes():
    for name in SCENES:
        if scene := bpy.data.scenes.get(name):
            assert scene != bpy.context.scene, "save editing projects with their normal entry scenes"
            for obj in list(scene.objects):
                if len(obj.users_scene) == 1:
                    bpy.data.objects.remove(obj, do_unlink=True)
            bpy.data.scenes.remove(scene)


if __name__ == "__main__":
    author()
