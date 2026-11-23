"""Produce the missing 90/180-degree turns and aim starts/stops."""
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_dodge_animations as existing
from build_support_animations import add_actor, bind_action, export_bank, save_checked
from check_support_delivery import check_bank
from civic_motion import Pose, rz
from directional_transition_motion import BRACE, DRIVE, REVISION, SPECS, path_at, pose_for, turn_parameters
from texture_pilot import ROOT, digest, write_json

BANK = ROOT/"Export/AnimationBanks/HUMAN_7H_Transitions.glb"
SCENE = "50_Directional_Transitions"


def clear_scene():
    if old := bpy.data.scenes.get(SCENE):
        for obj in list(old.objects):
            if len(obj.users_scene)==1:
                bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.scenes.remove(old)


def author():
    files = (existing.MASTER,existing.CIVIC)
    hashes = [digest(file) for file in files]
    bpy.ops.wm.open_mainfile(filepath=str(files[0]))
    entry = bpy.context.scene.name
    clear_scene()
    rig = bpy.data.objects["HUMAN_7H"]
    existing.human = rig
    existing.asset_context()
    existing.load_functions("build_civic_animations.py","Clearance","geometry_signature")
    existing.load_functions("verify_civic_animations.py","bind")
    existing.load_functions("build_resource_pack.py","clone_actor","studio","ground","text_label")
    work = bpy.data.scenes.new("Directional_Transitions_Authoring")
    work.collection.objects.link(rig)
    bpy.context.window.scene = work
    work.render.fps = 30
    sample = existing.sampler(work,rig)
    floor = existing.Clearance()
    new_names = {"HUMAN_7H__"+name for name in SPECS}
    before = {a.name:existing.action_signature(a) for a in bpy.data.actions if a.name not in new_names}
    binding = existing.bind_pose(rig)
    geometry = existing.geometry_signature()
    stats = {}
    transitions = {}
    for name,(frames,mode,side,angle) in SPECS.items():
        samples = [(i/(frames*2),pose_for(name,i/(frames*2),sample)) for i in range(frames*2+1)]
        for _,pose in samples[1:-1]:
            floor.correct(pose)
        min_z = min(floor.minimum(p) for _,p in samples)
        assert min_z >= -.005 and all(p.basis["root"].is_identity for _,p in samples), (name,min_z)
        action = existing.bake(rig,"HUMAN_7H__"+name,[p for _,p in samples])
        action["motion_revision"] = REVISION
        stats[name] = {"duration_seconds":frames/30,"loop":False,"minimum_skin_z_m":min_z}
        if mode == "turn":
            _,feet,_,_ = turn_parameters(side)
            contact = {}
            for foot,(begin,end) in zip(feet,(BRACE,DRIVE)):
                locked = []
                for t,p in samples:
                    if begin <= t <= end:
                        offset,yaw = path_at(side,angle,t)
                        locked.append(offset + rz(yaw) @ p.fk("foot."+foot).translation)
                drift = max((v.xy-locked[0].xy).length for v in locked)
                assert drift < .025, (name,foot,drift)
                contact[foot] = {"start_phase":begin,"end_phase":end,"max_reference_motion_xy_m":drift}
            transitions[name] = {
                "layer":"full_body or human_lower_body with pistol carry",
                "matching_gait":"Run" if angle>2 else "Walk",
                "entry_phase":0 if side=="Left" else .5,"exit_phase":.5 if side=="Left" else 0,
                "reference_turn_deg":math.degrees(angle)*(1 if side=="Left" else -1),
                "reference_only_actor_path":[{"phase":t,"actor_offset_m":list(path_at(side,angle,t)[0]),
                                              "actor_yaw_deg":math.degrees(path_at(side,angle,t)[1])} for t,_ in samples],
                "visual_events":{"brake_plant":BRACE[0],"turn_mid":.5,"push_off":DRIVE[0],"recover":1},
                "contacts":contact,
            }
        else:
            gait = "Aim_Walk_"+side
            phase = .5 if side=="Right" else 0
            transitions[name] = {
                "layer":"human_lower_body, maintain aim upper body",
                "entry_clip":"Aim_Base" if mode=="Start" else gait,
                "exit_clip":gait if mode=="Start" else "Aim_Base",
                "entry_phase":0 if mode=="Start" else phase,
                "exit_phase":phase if mode=="Start" else 0,
                "visual_events":{"plant_or_release":.5,"recover":1},
            }
        print("DIRECTIONAL_TRANSITION_BAKED",name,flush=True)
    assert before == {name:existing.action_signature(bpy.data.actions[name]) for name in before}
    assert binding == existing.bind_pose(rig) and geometry == existing.geometry_signature()
    bank_scene = export_bank(rig,BANK,SPECS)
    scene,col = existing.studio(SCENE,(2.7,-8,3.4),(0,0,.9),6.7,(920,580))
    existing.ground(col,10,-.008)
    bpy.context.window.scene = scene
    scene.frame_start,scene.frame_end = 0,16
    examples = (("Player_Explorer","Run_Turn180_Left","180 LEFT"),
                ("Ria_Engineer","Run_Turn180_Right","180 RIGHT"),
                ("NPC_HeavyCombat","Aim_Strafe_Start_Right","AIM STEP"))
    for i,(recipe,name,label) in enumerate(examples):
        x = (i-1)*2
        actor = add_actor(col,rig,recipe,"50_"+label,(x,0,0))
        bind_action(actor,bpy.data.actions["HUMAN_7H__"+name])
        frames,mode,side,angle = SPECS[name]
        if mode == "turn":
            pivot = bpy.data.objects.new("Reference_Path_"+label,None)
            col.objects.link(pivot)
            actor.parent = pivot
            actor.location = (0,0,0)
            for k in range(frames*2+1):
                offset,yaw = path_at(side,angle,k/(frames*2))
                pivot.location = Vector((x,0,0)) + offset
                pivot.rotation_euler.z = yaw
                pivot.keyframe_insert("location",frame=k/2)
                pivot.keyframe_insert("rotation_euler",frame=k/2)
            for curve in pivot.animation_data.action.fcurves:
                for key in curve.keyframe_points:
                    key.interpolation = "LINEAR"
        existing.text_label(col,label,(x,-.75,.012),.16)
    scene.frame_set(8)
    if "--render" in sys.argv:
        preview = ROOT/"Preview/Support"
        preview.mkdir(parents=True,exist_ok=True)
        scene.cycles.samples = 8
        for index in range(9):
            scene.frame_set(index*2)
            scene.render.filepath = str(preview/f"Turn_{index:02}.png")
            bpy.ops.render.render(write_still=True)
    bpy.context.window.scene = bpy.data.scenes[entry]
    rig.animation_data.action = None
    Pose(rig).apply()
    bpy.data.scenes.remove(work)
    bpy.data.scenes.remove(bank_scene)
    bpy.context.preferences.filepaths.save_version = 0
    check = check_bank({"clips":stats},BANK)
    check["shared_transition_clip_count"] = check.pop("shared_support_clip_count")
    save_checked(files[0],hashes[0])

    bpy.ops.wm.open_mainfile(filepath=str(files[1]))
    clear_scene()
    original = {a.name:existing.action_signature(a) for a in bpy.data.actions if a.name not in new_names}
    for name in new_names:
        if action := bpy.data.actions.get(name):
            bpy.data.actions.remove(action,do_unlink=True)
    with bpy.data.libraries.load(str(files[0]),link=False) as (_,out):
        out.actions = sorted(new_names)
        out.scenes = [SCENE]
    for name in new_names:
        bpy.data.actions[name].use_fake_user = True
    assert original == {name:existing.action_signature(bpy.data.actions[name]) for name in original}
    save_checked(files[1],hashes[1])
    write_json(ROOT/"Documentation/directional_transitions.json",{
        "schema":"erma.directional_transitions.v1", "bank":BANK.relative_to(ROOT).as_posix(),
        "sha256":digest(BANK),"rig":"HUMAN_7H","source_review_scene":SCENE,
        "root":"fixed; scene object path demonstrates crop-free controller yaw and anchored plants",
        "reference":"Mixamo Running Turn 180 public preview checked 2026-10-29 per ANIMATION_BLEND_REQUEST.md; authored on HUMAN_7H",
        "clips":{name:{**stats[name],**row} for name,row in transitions.items()},
    })
    write_json(ROOT/"Validation/directional_transitions.json",{
        **check,"previous_master_actions_preserved":len(before),"previous_civic_actions_preserved":len(original),
        "all_checks_passed":True})
    print("DIRECTIONAL_TRANSITIONS_READY",len(SPECS),flush=True)


if __name__ == "__main__":
    author()
