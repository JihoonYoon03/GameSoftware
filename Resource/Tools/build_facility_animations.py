"""Author CQ-11B action banks without re-exporting the existing character meshes."""
from pathlib import Path
import json
import math
import sys

import bpy
from mathutils import Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_dodge_animations import bake
from civic_motion import Pose, pulse, rx, ry, rz, smooth
from verify_human_reference_v4 import skeleton
from verify_resource_pack import accessor, read_glb

ROOT = Path(__file__).resolve().parents[1]
ASSEMBLY = ROOT/"Source/Characters/Field_Medic_Mixamo_Assembly.blend"
LIBRARY = ROOT/"Source/Erma_Resource_Library.blend"
LEGACY_REFERENCE = ROOT/"Export/Characters/Player_Explorer.glb"
MIXAMO_REFERENCE = ROOT/"Export/HumanV4/FieldMedicMixamo/Field_Medic_Boots_LOD0.glb"

# Frames at 30 fps. The authoring timeline also records each half-frame sample.
CLIPS = {
    "Shop_Idle_Counter_Loop": (108, True, {}),
    "Shop_Talk_Welcome": (37, False, {"welcome_peak": .43, "recover": .93}),
    "Shop_Trade_HandOver": (34, False, {"handover_peak": .49, "nod_peak": .66, "recover": .94}),
    "Shop_Talk_Explain_Loop": (96, True, {}),
    "Shop_Farewell_Nod": (29, False, {"nod_peak": .43, "recover": .91}),
    "Shop_Service_Inspect_Loop": (96, True, {}),
    "Resident_Talk_Listen_Loop": (90, True, {"nod_peak": .58}),
}
BASELINE = {
    "Idle": True, "Walk": True, "Run": True, "Aim_Base": False,
    "Dodge_Left_L": False, "Support_Inspect_Loop": True,
}
for pace in ("Walk", "Run"):
    for direction in ("Forward", "ForwardLeft", "Left", "BackLeft",
                      "Back", "BackRight", "Right", "ForwardRight"):
        BASELINE[f"Aim_{pace}_{direction}"] = True
COMMON = {"pelvis":"Hips","spine":"Spine","chest":"Spine2","neck":"Neck","head":"Head"}
for side, label in (("L","Left"),("R","Right")):
    for role,bone in (("clavicle","Shoulder"),("upper_arm","Arm"),("forearm","ForeArm"),
                      ("hand","Hand"),("thigh","UpLeg"),("shin","Leg"),("foot","Foot")):
        COMMON[f"{role}.{side}"] = label+bone


def motion(name, t):
    # (left lift, right lift, hand roll, head pitch, head yaw, thorax yaw)
    left = right = palm = nod = glance = turn = 0.
    if name=="Shop_Idle_Counter_Loop":
        glance=.025*math.sin(math.tau*t)
    elif name=="Shop_Talk_Welcome":
        right=pulse(t,.03,.43,.83)
        palm=.40*pulse(t,.13,.48,.77)
        nod=.12*pulse(t,.27,.52,.78)
        turn=.05*right
    elif name=="Shop_Trade_HandOver":
        right=pulse(t,.03,.49,.91)
        left=.18*pulse(t,.10,.43,.82)
        palm=.25*right
        nod=.17*pulse(t,.48,.66,.87)
        turn=.055*right
    elif name=="Shop_Talk_Explain_Loop":
        right=.68*pulse(t,.08,.33,.66)
        left=.59*pulse(t,.33,.63,.95)
        palm=.32*(left+right)
        glance=.065*(left-right)
        turn=.13*(right-left)
    elif name=="Shop_Farewell_Nod":
        nod=.23*pulse(t,.09,.43,.75)
        right=.19*pulse(t,.16,.45,.79)
    elif name=="Shop_Service_Inspect_Loop":
        left=.67*pulse(t,.05,.38,.91)
        right=.94*pulse(t,.21,.51,.86)
        nod=.27*pulse(t,.05,.44,.88)
        glance=.14*pulse(t,.16,.47,.92)
        turn=-.08*left
    elif name=="Resident_Talk_Listen_Loop":
        glance=-.055*pulse(t,.09,.34,.88)
        nod=.17*pulse(t,.37,.58,.78)
        left=.18*pulse(t,.39,.66,.92)
    return left,right,palm,nod,glance,turn


def authored_pose(rig, names, name, t):
    pose = Pose(rig)
    smooth_cycle=(1-math.cos(math.tau*t))*.5
    left,right,palm,nod,glance,turn=motion(name,t)
    weight_shift = .018 * math.sin(math.tau*t)
    pose.relative(names["spine"],rx(.023+.016*smooth_cycle)@rz(turn*.31)@ry(weight_shift))
    pose.relative(names["chest"],rx(-.014+.014*smooth_cycle)@rz(turn*.70)@ry(.014*(left-right)))
    pose.relative(names["neck"],rx(nod*.31)@rz(glance*.45))
    pose.relative(names["head"],rx(nod*.69-.012*smooth_cycle)@rz(glance*.55-turn*.36))
    for side,sign,lift in (("L",1,left),("R",-1,right)):
        upper,lower,end=(names[part+"."+side] for part in ("upper_arm","forearm","hand"))
        pose.relative(names["clavicle."+side],rx(.015*lift)@ry(sign*.044*lift))
        wrist=Vector((sign*.267,-.113,1.015+.007*smooth_cycle))
        raised=Vector((sign*.32,-.330,1.275))
        reach=Vector((sign*.016,-1,.20))
        dorsal=Vector((sign*.4,.2,1))
        pole=Vector((sign*.34,.91,-.46))
        if name=="Shop_Talk_Welcome":
            raised=Vector((sign*.325,-.345,1.395))
            reach=Vector((sign*.13,-.31,.76))
            dorsal=Vector((sign*.18,1,.15))
        elif name=="Shop_Trade_HandOver":
            raised=Vector((sign*.255,-.437,1.140))
            reach=Vector((sign*.10,-1,.06))
            dorsal=Vector((sign*.38,.10,-1))
        elif name=="Shop_Service_Inspect_Loop":
            raised=Vector((sign*.220,-.360,1.145-.018*pulse(t,.48,.57,.65)))
            reach=Vector((sign*.20,-.96,-.26))
            dorsal=Vector((sign*.32,.05,1))
        target=wrist.lerp(raised,lift)
        direction=Vector((sign*.075,-.20,-1)).lerp(reach,lift).normalized()
        rest_vote=pose.rest[end].to_quaternion()@Vector((0,1,0))
        orient=rest_vote.rotation_difference(direction)
        rest_normal=Vector((0,0,1))
        default_dorsal=Vector((sign,0,.10))
        gesture_dorsal=default_dorsal.lerp(dorsal,lift)
        current=orient@rest_normal
        current=(current-direction*current.dot(direction)).normalized()
        desired=(gesture_dorsal-direction*gesture_dorsal.dot(direction)).normalized()
        roll=math.atan2(direction.dot(current.cross(desired)),current.dot(desired))
        orient=Quaternion(direction,roll+sign*palm*.1)@orient
        pose.chain(upper,lower,end,target,pole,end_delta=orient)
    return pose


def retarget_pose(mixamo, legacy):
    pose=Pose(mixamo)
    for old, label in COMMON.items():
        new="mixamorig:"+label
        # Match the longitudinal axes as well as the motion delta. The two armatures
        # have different arm angles in bind pose.
        a=pose.rest[new].to_quaternion()@Vector((0,1,0))
        b=legacy.rest[old].to_quaternion()@Vector((0,1,0))
        align=a.rotation_difference(b)
        pose.absolute(new,legacy.deformation(old).to_quaternion()@align)
        if old=="pelvis":
            pose.translate(new,(0,0,legacy.deformation(old).translation.z))
    return pose


def sample_source(scene, rig, action, frame):
    animation = rig.animation_data
    if animation.action != action:
        animation.action = action
        animation.action_slot = action.slots[0]
        animation.use_nla = False
    start, end = action.frame_range
    time = min(start + frame, end)
    scene.frame_set(math.floor(time), subframe=time % 1)
    evaluated = rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    pose = Pose(rig)
    pose.basis = {bone.name: bone.matrix_basis.copy() for bone in evaluated.pose.bones}
    return pose


def legacy_source():
    with bpy.data.libraries.load(str(LIBRARY),link=False) as (source,target):
        target.objects=["HUMAN_7H"]
        target.actions=["HUMAN_7H__"+name for name in BASELINE]
    rig,=target.objects
    assert all(target.actions),[(name,action) for name,action in zip(BASELINE,target.actions)]
    rig.animation_data_clear()
    rig.animation_data_create()
    scene=bpy.data.scenes.new("Legacy Motion Authoring")
    scene.render.fps=30
    scene.collection.objects.link(rig)
    return rig,scene


def reference_anchor(rig, scene):
    data=bpy.data.meshes.new("Hide in game | bind reference")
    data.from_pydata([(0,0,0),(.001,0,0),(0,0,.001)],[],[(0,1,2)])
    data.update()
    obj=bpy.data.objects.new("BindReference",data)
    scene.collection.objects.link(obj)
    obj["attachment_role"]="bind_reference_not_rendered"
    obj.parent=rig
    group=obj.vertex_groups.new(name=rig.data.bones[0].name)
    group.add([0,1,2],1,"REPLACE")
    obj.modifiers.new("Bind Reference","ARMATURE").object=rig
    return obj


def register_action(rig, prefix, name, rows, loop, markers):
    rig.animation_data_create()
    action=bake(rig,prefix+"__"+name,rows)
    action["loop"]=loop
    action["motion_revision"]="cq11b_facility_v1"
    for event,phase in markers.items():
        action.pose_markers.new(event).frame=round(phase*((len(rows)-1)/2))
    return action


def export_bank(path, rig, actions):
    scene=bpy.data.scenes.new(path.stem+" Export")
    bpy.context.window.scene=scene
    scene.render.fps=60
    bank=rig.copy()
    bank.data=rig.data.copy()
    scene.collection.objects.link(bank)
    bank.name=path.stem
    bank.data.pose_position="POSE"
    bank.animation_data_clear()
    animation=bank.animation_data_create()
    Pose(bank).apply()
    for name,action in actions.items():
        track=animation.nla_tracks.new()
        track.name=name
        strip=track.strips.new(name,0,action)
        strip.name=name
        strip.scale=2
    anchor=reference_anchor(bank,scene)
    for item in scene.objects:
        item.select_set(True)
    bpy.context.view_layer.objects.active=bank
    scene.frame_set(0)
    bpy.ops.export_scene.gltf(filepath=str(path),export_format="GLB",use_selection=True,
        use_active_scene=True,export_animations=True,export_animation_mode="NLA_TRACKS",
        export_force_sampling=True,export_frame_range=False,export_frame_step=1,
        export_skins=True,export_def_bones=False,export_yup=True,
        export_anim_slide_to_zero=True,export_optimize_animation_size=True,export_extras=True)
    bpy.data.objects.remove(anchor,do_unlink=True)
    data=bank.data
    bpy.data.objects.remove(bank,do_unlink=True)
    bpy.data.armatures.remove(data)
    return scene


def review(scene, rig, actions, output):
    bpy.context.window.scene=scene
    camera_data=bpy.data.cameras.new("Eye-level counter")
    camera=bpy.data.objects.new("Eye-level counter",camera_data)
    scene.collection.objects.link(camera)
    camera.location=(.42,-3.48,1.65)
    camera.rotation_euler=(Vector((0,0,1.34))-camera.location).to_track_quat("-Z","Y").to_euler()
    camera_data.type="ORTHO"
    camera_data.ortho_scale=1.67
    scene.camera=camera
    world=bpy.data.worlds.new("Facility studio")
    world.use_nodes=True
    world.node_tree.nodes["Background"].inputs[0].default_value=(.25,.29,.35,1)
    world.node_tree.nodes["Background"].inputs[1].default_value=.53
    scene.world=world
    for label,point,energy,size in (
        ("Key",(-2,-3,3),320,2.6),("Fill",(2.3,-1.7,2),145,2.1),("Edge",(0,2,2.7),220,1.8)):
        lamp=bpy.data.lights.new(label,"AREA")
        lamp.energy,lamp.size=energy,size
        light=bpy.data.objects.new(label,lamp)
        scene.collection.objects.link(light)
        light.location=point
        light.rotation_euler=(Vector((0,0,1.36))-light.location).to_track_quat("-Z","Y").to_euler()
    scene.render.engine="CYCLES"
    scene.cycles.samples=20
    scene.cycles.use_denoising=True
    scene.render.resolution_x=1280
    scene.render.resolution_y=800
    scene.render.resolution_percentage=100
    for name,phase in (("Shop_Idle_Counter_Loop",.20),("Shop_Talk_Welcome",.43),
                       ("Shop_Trade_HandOver",.49),("Shop_Talk_Explain_Loop",.62),
                       ("Shop_Service_Inspect_Loop",.52)):
        action=actions[name]
        rig.animation_data.action=action
        rig.animation_data.action_slot=action.slots[0]
        bpy.context.view_layer.update()
        scene.frame_end=CLIPS[name][0]
        scene.frame_set(round(phase*scene.frame_end))
        scene.render.filepath=str(output/(name+".png"))
        bpy.ops.render.render(write_still=True)
    name="Shop_Idle_Counter_Loop"
    rig.animation_data.action=actions[name]
    rig.animation_data.action_slot=actions[name].slots[0]
    scene.frame_end=CLIPS[name][0]
    scene.frame_set(17)
    scene.render.filepath="//../Preview/Facility/Shop_Idle_Counter_Loop.png"
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=="VIEW_3D":
                area.spaces.active.region_3d.view_perspective="CAMERA"
                area.spaces.active.overlay.show_overlays=False
                area.spaces.active.shading.type="MATERIAL"


def check_bank(path, reference, authored):
    doc,binary=read_glb(path)
    a=skeleton(doc,binary)
    b=skeleton(*read_glb(reference))
    assert [x[:2] for x in a]==[x[:2] for x in b]
    bind_error=max(abs(s-t) for (_,_,m),(_,_,n) in zip(a,b) for s,t in zip(m,n))
    assert bind_error<2e-6, (path.name,bind_error)
    assert len(doc["meshes"])==1
    rows={}
    for clip in doc["animations"]:
        name=clip["name"]
        sampler_list=clip["samplers"]
        duration=max(accessor(doc,binary,s["input"])[-1][0] for s in sampler_list)
        if name in CLIPS:
            frames,loop,_=CLIPS[name]
            assert abs(duration-frames/30)<2e-5,(name,duration,frames)
            root=a[0][0]
            for channel in clip["channels"]:
                target=channel["target"]
                samples=accessor(doc,binary,sampler_list[channel["sampler"]]["output"])
                if doc["nodes"][target["node"]]["name"]==root:
                    assert max(abs(x-y) for sample in samples for x,y in zip(sample,samples[0]))<1e-6
                if loop:
                    assert max(abs(x-y) for x,y in zip(samples[0],samples[-1]))<2e-5,(name,target)
        assert len(clip["channels"])>1
        rows[name]=round(duration,6)
    assert set(rows)==set(authored),(path.name,rows,set(authored))
    return {"bones":len(a),"inverse_bind_max_error":bind_error,"durations_s":rows}


def main():
    output=Path(sys.argv[sys.argv.index("--")+1]).resolve()
    output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(ASSEMBLY))
    medic=bpy.context.scene
    medic.name="Field Medic | facility close-up"
    medic.render.fps=30
    mixamo=bpy.data.objects["Mixamo_Master_Rig"]
    mixamo.data.pose_position="POSE"
    old,old_scene=legacy_source()
    old_names={name:name for name in COMMON}
    mix_names={name:"mixamorig:"+mapped for name,mapped in COMMON.items()}
    made={"HUMAN_7H":{},"Mixamo_22":{}}
    for rig,scene,names,prefix in ((old,old_scene,old_names,"HUMAN_7H"),(mixamo,medic,mix_names,"Mixamo_22")):
        bpy.context.window.scene=scene
        for name,(frames,loop,markers) in CLIPS.items():
            rows=[authored_pose(rig,names,name,index/(2*frames)) for index in range(2*frames+1)]
            if loop:
                rows[-1]=rows[0]
            made[prefix][name]=register_action(rig,prefix,name,rows,loop,markers)
    bpy.context.window.scene=old_scene
    for name,loop in BASELINE.items():
        action=bpy.data.actions["HUMAN_7H__"+name]
        duration=float(action.frame_range[1]-action.frame_range[0])
        frames=max(1,round(duration))
        rows=[retarget_pose(mixamo,sample_source(old_scene,old,action,i/2))
              for i in range(frames*2+1)]
        bpy.context.window.scene=medic
        if loop:
            rows[-1]=rows[0]
        made["Mixamo_22"][name]=register_action(mixamo,"Mixamo_22",name,rows,loop,{})
        bpy.context.window.scene=old_scene
    contract={"request":"CQ-11B","authored_sample_rate_hz":60,
              "clips":{name:{"duration_s":frames/30,"loop":loop,"events":markers}
                       for name,(frames,loop,markers) in CLIPS.items()},
              "mixamo_retargeted_from_human_7h":BASELINE,
              "interruption_blend_seconds":.2,"owns_gameplay_events":False,
              "root_motion":"in_place","banks":{}}
    for prefix,rig,reference in (("HUMAN_7H",old,LEGACY_REFERENCE),("Mixamo_22",mixamo,MIXAMO_REFERENCE)):
        path=output/(prefix+"_Facility.glb")
        export_scene=export_bank(path,rig,made[prefix])
        bpy.context.window.scene=medic
        bpy.data.scenes.remove(export_scene)
        contract["banks"][path.name]=check_bank(path,reference,made[prefix])
    review(medic,mixamo,made["Mixamo_22"],output)
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(output/"Facility_Talk_Animations.blend"),compress=True)
    (output/"facility_animation_bank.json").write_text(json.dumps(contract,indent=2)+"\n",encoding="utf8")
    print("FACILITY", {name:len(result["durations_s"]) for name,result in contract["banks"].items()})


if __name__=="__main__":
    main()
