"""Stage both review projects and the append-only bank for CQ-05B."""
import hashlib
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_dodge_animations as pipeline
from civic_motion import Pose, blend, smooth
from dodge_motion import DURATION, FPS, MOVE_SECONDS, SPECS, direction, reference, travel
from verify_dodge_animations import PISTOL, masked

SHOWCASE = "45_Dodge_Direction_Review"
BRANCHES = "46_Dodge_Interrupt_Review"


def review_scene(name, examples, sample, rig, moving):
    width = 3.5 if moving else 1.9
    target = (0, -1.35 if moving else 0, .85)
    scene, col = pipeline.studio(name, (2, -14, 7), target, width * len(examples) + .8,
                                 (1400, 680) if moving else (1200, 560))
    pipeline.ground(col, 14, -.008)
    if moving:
        scene.world.node_tree.nodes["Background"].inputs[1].default_value = .65
        for suffix, x in [("Key", -6), ("Fill", 6)]:
            light = bpy.data.objects[name + "_" + suffix]
            light.location.x = x
            light.data.energy = 1500
            light.data.size = 10
    scene.render.fps = FPS
    scene.frame_start = 0
    scene.frame_end = 18 if moving else 12
    scene["instructions"] = (
        "Object translation is a timing reference only. Review different cancel targets at "
        "the same 0.10 s interruption; the runtime selects the real target and collision point."
        if moving else "Compare the unarmed dodge with carry and aim masks on the same lower-body steps."
    )
    bpy.context.window.scene = scene
    for index, (label, recipe, clip, mode, branch) in enumerate(examples):
        x = (index - (len(examples) - 1) / 2) * width
        info = pipeline.recipes[recipe]
        parts = [part for part in info["parts"] if part != PISTOL or mode != "unarmed"]
        if mode != "unarmed" and PISTOL not in parts:
            parts.append(PISTOL)
        actor, _ = pipeline.clone_actor(name[:2] + "_" + label, rig, parts, col, (x, 0, 0),
                                         info.get("skin"), info.get("offsets"))
        pipeline.text_label(col, label, (x, -4.3 if moving else -.64, .015), .24 if moving else .14)
        path = None
        if moving:
            # Separate the review-only object track from the skin action.
            path = bpy.data.objects.new(actor.name + "_ReviewPath", None)
            col.objects.link(path)
            actor.parent = path
            actor.location = (0, 0, 0)
        name_dir, push = SPECS[clip]
        base, entry, exit_phase = reference(name_dir, push)

        def compose(seconds):
            phase = min(1, seconds / DURATION)
            if seconds > DURATION:
                lower = sample(base, (exit_phase + (seconds - DURATION) * 1.5) % 1)
            else:
                lower = sample(clip, phase)
            if mode == "aim":
                return masked(lower, sample("Aim_Base", 0))
            if mode == "carry":
                return masked(lower, sample("Pistol_Carry_Run", (entry + phase * .5) % 1))
            return lower

        interrupt = .10
        if branch == "ride":
            info = pipeline.read("Documentation/vehicle_assemblies.json")["types"]["JetBike"]
            bike, _ = pipeline.clone_actor("46_Mount_JetBike", bpy.data.objects[info["skeleton"]],
                                           info["default_parts"], col, (x, -travel(interrupt), 0))
            bike.rotation_euler.z = -math.pi / 2
            Pose(bike).apply()
        poses = []
        for index in range(scene.frame_end * 2 + 1):
            seconds = index / 60
            pose = compose(seconds)
            path_time = seconds
            if branch and seconds > interrupt:
                path_time = interrupt
                elapsed = seconds - interrupt
                cut_pose = compose(interrupt)
                if branch == "menu":
                    # Hold the evaluated pose while the menu is up, then ease back to idle.
                    pose = blend(cut_pose, sample("Idle", 0), (elapsed - .18) / .12)
                elif branch == "hit":
                    pose = blend(cut_pose, sample("Hit_Front", min(1, elapsed / .34)), elapsed / .08)
                else:
                    target = "Ride_Base" if branch == "ride" else "Idle"
                    pose = blend(cut_pose, sample(target, 0), elapsed / .12)
            poses.append(pose)
            if path:
                distance = travel(path_time) + max(0, path_time - MOVE_SECONDS) * 2.46
                path.location = Vector((x, 0, 0)) + direction(name_dir) * distance
                if branch == "ride":
                    path.location.z = -.076 * smooth((seconds - interrupt) / .12)
                path.keyframe_insert("location", frame=index / 2)
        if path:
            for curve in path.animation_data.action.fcurves:
                for key in curve.keyframe_points:
                    key.interpolation = "LINEAR"

        review_poses = []
        for sampled in poses:
            pose = Pose(actor)
            pose.basis = sampled.basis
            review_poses.append(pose)
        pipeline.bake(actor, f"DodgeReview__{scene.name[:2]}_{label}", review_poses)
    scene.frame_set(6)


def export_bank(rig):
    scene = bpy.data.scenes.new("Dodge_Animation_Bank")
    scene.render.fps = 60
    bpy.context.window.scene = scene
    copy = rig.copy()
    copy.data = rig.data.copy()
    scene.collection.objects.link(copy)
    copy.animation_data_clear()
    Pose(copy).apply()
    pipeline.attach_tracks(copy, muted=False, scale=2)
    weapon = bpy.data.objects[PISTOL].copy()
    scene.collection.objects.link(weapon)
    weapon.parent = copy
    for modifier in weapon.modifiers:
        if modifier.type == "ARMATURE":
            modifier.object = copy
    for obj in scene.objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = copy
    scene.frame_set(0)
    bpy.ops.export_scene.gltf(
        filepath=str(pipeline.STAGE / "Dodge_Animation_Bank.glb"), export_format="GLB",
        use_selection=True, use_active_scene=True, export_animations=True,
        export_animation_mode="NLA_TRACKS", export_force_sampling=True,
        export_frame_range=False, export_frame_step=1, export_skins=True,
        export_def_bones=False, export_yup=True, export_anim_slide_to_zero=True,
        export_optimize_animation_size=True,
    )
    bpy.data.objects.remove(weapon, do_unlink=True)
    bones = copy.data
    bpy.data.objects.remove(copy, do_unlink=True)
    bpy.data.armatures.remove(bones)
    return scene


def stage():
    proof = pipeline.read("Validation/dodge_authoring.json")
    assert pipeline.read("Validation/dodge_equipment.json")["all_checks_passed"]
    assert hashlib.sha256(pipeline.MASTER.read_bytes()).hexdigest() == proof["master_sha256"]
    rig = bpy.data.objects["HUMAN_7H"]
    old_actions = pipeline.existing_signatures()
    entry = bpy.context.scene.name
    pipeline.asset_context()
    pipeline.load_functions("build_resource_pack.py", "clone_actor", "studio", "ground", "text_label")
    pipeline.load_functions("verify_civic_animations.py", "bind")
    bank = export_bank(rig)
    bpy.context.window.scene = bpy.data.scenes[entry]
    bpy.data.scenes.remove(bank)
    source_scene = bpy.data.scenes.new("Dodge_Review_Sampling")
    source_scene.render.fps = FPS
    source_scene.collection.objects.link(rig)
    bpy.context.window.scene = source_scene
    sample = pipeline.sampler(source_scene, rig)

    # Sample the canonical poses before building the separate review scenes.
    for clip, (name, push) in SPECS.items():
        base, entry_phase, exit_phase = reference(name, push)
        for i in range(37):
            seconds = i / 60
            sample(clip, min(1, seconds / DURATION))
            sample("Pistol_Carry_Run", (entry_phase + min(1, seconds / DURATION) * .5) % 1)
            sample(base, (exit_phase + (seconds - DURATION) * 1.5) % 1)
    for name in ("Aim_Base", "Idle", "Ride_Base"):
        sample(name, 0)
    for i in range(7, 37):
        sample("Hit_Front", min(1, (i / 60 - .10) / .34))

    review_scene(SHOWCASE, [
        ("FORWARD L", "Player_Explorer", "Dodge_Forward_L", "unarmed", None),
        ("DIAGONAL R", "Ria_Engineer", "Dodge_ForwardLeft_R", "carry", None),
        ("BACK L", "NPC_HeavyCombat", "Dodge_Back_L", "aim", None),
        ("RIGHT R", "Kain_Guard", "Dodge_Right_R", "aim", None),
    ], sample, rig, False)
    review_scene(BRANCHES, [
        ("CONTINUE RUN", "Player_Explorer", "Dodge_Forward_R", "unarmed", None),
        ("WALL TO IDLE", "Player_Explorer", "Dodge_Forward_L", "unarmed", "wall"),
        ("MENU HOLD", "Ria_Engineer", "Dodge_Forward_R", "unarmed", "menu"),
        ("HIT REACTION", "NPC_HeavyCombat", "Dodge_Forward_L", "unarmed", "hit"),
        ("MOUNT POSE", "Kain_Guard", "Dodge_Forward_R", "unarmed", "ride"),
    ], sample, rig, True)
    assert all(pipeline.action_signature(bpy.data.actions[name]) == digest
               for name, digest in old_actions.items())
    rig.animation_data.action = None
    Pose(rig).apply()
    bpy.context.window.scene = bpy.data.scenes[entry]
    bpy.data.scenes.remove(source_scene)
    for actor in bpy.data.objects:
        if actor.type == "ARMATURE" and set(actor.data.bones.keys()) == set(rig.data.bones.keys()):
            pipeline.attach_tracks(actor)
    bpy.context.preferences.filepaths.save_version = 0
    master = pipeline.STAGE / pipeline.MASTER.name
    bpy.ops.wm.save_as_mainfile(filepath=str(master), compress=True)

    human_bones = set(rig.data.bones.keys())
    bpy.ops.wm.open_mainfile(filepath=str(pipeline.CIVIC))
    civic_entry = bpy.context.scene.name
    old_civic = pipeline.existing_signatures()
    old_scenes = set(bpy.data.scenes.keys())
    with bpy.data.libraries.load(str(master), link=False) as (source, destination):
        destination.actions = ["HUMAN_7H__" + name for name in SPECS]
        destination.scenes = [SHOWCASE, BRANCHES]
    for actor in bpy.data.objects:
        if actor.type == "ARMATURE" and set(actor.data.bones.keys()) == human_bones:
            pipeline.attach_tracks(actor)
    assert all(pipeline.action_signature(bpy.data.actions[name]) == digest
               for name, digest in old_civic.items())
    assert old_scenes <= set(bpy.data.scenes.keys())
    assert bpy.context.scene.name == civic_entry
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(pipeline.STAGE / pipeline.CIVIC.name), compress=True)
    print("DODGE_PROJECTS_STAGED", len(SPECS), flush=True)


if __name__ == "__main__":
    stage()
