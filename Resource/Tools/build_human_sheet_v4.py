"""Build the reference-sheet-only human pack with original tiled textures."""
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Quaternion, Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
from build_human_reference_v4 import ROOT, bounds, make_part
from human_equipment_atlas import write_equipment_atlas
from human_sheet_additions import GUARD_VARIANT, NEW_CHARACTERS, extra_cuff, extra_parts, helmet, pouch
from human_sheet_equipment import kit_parts
from human_sheet_forms import FIGURES, KITS, SKIN, boots, features, feet, fitted_body, hair, hands, head
from human_sheet_maps import write_maps


BLEND=ROOT/"Source/Human_Drawing_Only_v4.blend"
EXPORT=ROOT/"Export/HumanSheet/Human_Drawing_Only_Parts.glb"
TEXTURES=ROOT/"Export/Textures/HumanSheet"
PREVIEWS=ROOT/"Preview/HumanSheet"
MANIFEST=ROOT/"Documentation/human_sheet_v4.json"
STAGES=("LOD0","LOD1","LOD2","ShadowProxy")
SHARED_ACCESSORIES={
    "Helmet_Guard":(GUARD_VARIANT,"Helmet","GUARD HELMET"),
    "Accessory_Shoulder_Tactical_L":("Guard","ShoulderCap_L","TACTICAL CAP / L"),
    "Accessory_Shoulder_Heavy_L":("NPC_HeavyCombat","ShoulderCap_L","HEAVY CAP / L"),
    "Accessory_Utility_Pouch_R":("Player_Explorer","UtilityPouch_R","UTILITY POUCH / R"),
    "Accessory_Medical_Injector_L":("Medic","Cuff_L","DIAGNOSTIC CUFF / L"),
    "Accessory_Medic_Pouch_L":("Medic","MedicalPouch_L","MEDICAL POUCH / L"),
    "Accessory_Work_Lamp_L":("Ria_Engineer","WorkLamp_L","WORK LAMP / L"),
    "Accessory_Work_Lamp_R":("NPC_Miner","WorkLamp_R","WORK LAMP / R"),
    "Accessory_Tool_Roll_L":("Ria_Engineer","ToolRoll_L","TOOL ROLL / L"),
}

PALETTE={
    "skin":"b87b5d","tan":"b47a5d","warm":"a1674d","lip":"8a5047","eye":"d4c6b4","ink":"31221d",
    "hair":"38241d","hairLight":"543724","neutral":"69645f","carbon":"27282b","charcoal":"413a34",
    "brown":"53473a","black":"252426","gunmetal":"4b4a49","stone":"b09b81","cream":"d2bca2",
    "slate":"354348","orange":"cf7928","amber":"ffc168","blue":"52627b","darkblue":"384557",
    "cyan":"92d9ef","metal":"8d8c88","earth":"75604d","sage":"545849","sand":"b6a18a",
    "coral":"bb6041","navy":"3b465b","blueSeam":"646d7b","rust":"a9592d","ochre":"ad7235",
    "khaki":"887666","glassBack":"85908a","cartridge":"c4ddd7","smoke":"9b8271","glassDark":"423a37",
    "strand":"ffffff","iris":"594537","lid_skin":"886858","lid_tan":"795b4d","lid_warm":"6a4a3d",
    "lip_skin":"a26f65","lip_tan":"95645c","lip_warm":"8b5951",
    "coal":"363638","dust":"555961","teal":"415b5b","lilac":"c69ce2","purpleMetal":"6f5f82",
    "copper":"815137","iron":"65626a","visor":"1c2023","lamp":"ffdda0",
    "researchGlass":"a8a8cf","safetyGlass":"b9c1b9",
}

def linear(hex_rgb):
    values=[int(hex_rgb[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in values)


def pbr_materials(paths):
    materials={}
    for key in (*paths,"Glass"):
        material=bpy.data.materials.new("HumanSheet_"+key)
        material.use_nodes=True
        nodes,links=material.node_tree.nodes,material.node_tree.links
        shader=nodes["Principled BSDF"]
        tint=nodes.new("ShaderNodeVertexColor")
        tint.layer_name="HumanTint"
        tint.location=(-650,280)
        color=tint.outputs["Color"]
        if key=="HairCards":
            texture=nodes.new("ShaderNodeTexImage")
            texture.image=bpy.data.images.load(str(paths[key]["BaseColor"]),check_existing=True)
            texture.image.colorspace_settings.name="sRGB"
            texture.extension="EXTEND"
            clip=nodes.new("ShaderNodeMath")
            clip.operation="ROUND"
            links.new(texture.outputs["Alpha"],clip.inputs[0])
            links.new(clip.outputs[0],shader.inputs["Alpha"])
            shader.inputs["Roughness"].default_value=.76
            shader.inputs["Specular IOR Level"].default_value=.23
            material.surface_render_method="DITHERED"
            color=texture.outputs["Color"]
        elif key in paths:
            maps={}
            for channel,file in paths[key].items():
                node=nodes.new("ShaderNodeTexImage")
                node.image=bpy.data.images.load(str(file),check_existing=True)
                node.image.colorspace_settings.name="sRGB" if channel=="BaseColor" else "Non-Color"
                node.extension="EXTEND" if key=="Equipment" else "REPEAT"
                node.label=channel
                node.location=(-900,{"BaseColor":550,"Normal":36,"ORM":-280}[channel])
                maps[channel]=node.outputs["Color"]
            mix=nodes.new("ShaderNodeMixRGB")
            mix.blend_type="MULTIPLY"
            mix.inputs[0].default_value=1.0
            mix.location=(-320,400)
            links.new(maps["BaseColor"],mix.inputs[1])
            links.new(color,mix.inputs[2])
            color=mix.outputs[0]
            normal=nodes.new("ShaderNodeNormalMap")
            normal.uv_map="UVMap"
            normal.inputs["Strength"].default_value={"Fabric":.7,"Shell":.49,"Skin":.26,"Equipment":.82}[key]
            normal.location=(-330,0)
            links.new(maps["Normal"],normal.inputs["Color"])
            links.new(normal.outputs[0],shader.inputs["Normal"])
            orm=nodes.new("ShaderNodeSeparateColor")
            orm.location=(-370,-250)
            links.new(maps["ORM"],orm.inputs["Color"])
            links.new(orm.outputs["Green"],shader.inputs["Roughness"])
            links.new(orm.outputs["Blue"],shader.inputs["Metallic"])
            group=bpy.data.node_groups.get("glTF Material Output")
            if group is None:
                group=bpy.data.node_groups.new("glTF Material Output","ShaderNodeTree")
                group.interface.new_socket(name="Occlusion",in_out="INPUT",socket_type="NodeSocketFloat")
            target=nodes.new("ShaderNodeGroup")
            target.node_tree=group
            target.location=(-65,-375)
            links.new(orm.outputs["Red"],target.inputs["Occlusion"])
        else:
            shader.inputs["Alpha"].default_value=.32
            shader.inputs["Roughness"].default_value=.27
            material.surface_render_method="BLENDED"
        links.new(color,shader.inputs["Base Color"])
        materials[key]=material
    return materials


def wardrobe(tag,stage,rig):
    if tag==GUARD_VARIANT:
        for slot,geometry in wardrobe("Guard",stage,rig):
            if slot not in ("Hair","Face"):
                yield slot,geometry
        yield "Helmet",helmet(stage)
        return
    kit=KITS.get(tag)
    profile=kit.profile if kit else tag
    figure=FIGURES[profile]
    skin=kit.skin if kit else "skin"
    style=kit.hair if kit else "crop"
    body,neck_part,arms,legs=fitted_body(rig,figure,stage,kit,skin)
    yield "Torso",body
    yield "Neck",neck_part
    skull=head(stage,figure,skin)
    yield "Head",skull
    if tag!="NPC_HeavyCombat":
        yield "Face",features(stage,style,skull,skin)
        yield "Hair",hair(stage,style)
    for side in ("L","R"):
        yield "Arm_"+side,arms[side]
        yield "Leg_"+side,legs[side]
        yield "Hand_"+side,hands(stage,side,skin,tag in ("Explorer","Guard","Pilot","NPC_Miner","NPC_HeavyCombat"))
        if kit:
            yield "Boot_"+side,boots(stage,side,kit,figure)
        else:
            yield "Foot_"+side,feet(stage,side,skin,figure)
    if tag in NEW_CHARACTERS:
        yield from extra_parts(tag,figure,stage,rig,body,arms,legs)
    elif kit:
        for slot,geometry in kit_parts(tag,figure,stage,rig,body,arms,legs):
            if tag=="Medic" and slot=="Cuff_L":
                geometry=extra_cuff(figure,stage,"L",rig,"medic")
            yield slot,geometry
        if tag=="Medic":
            yield "MedicalPouch_L",pouch(figure,stage,"L","medic")


def shadow_wardrobe(tag,rig):
    kept=("Torso","Neck","Head","Arm_","Leg_","Hand_","Boot_","Foot_","Hair","CoatTail_",
          "ShoulderCap_","Helmet","DualLifeSupport","DorsalPack","RescuePack","FlightSupportPack","CommuterPack",
          "DorsalPowerPack","IndustrialPowerRig","SampleRack","Hardhat","Respirator","ExoskeletonStrut_")
    for name,g in wardrobe(tag,2,rig):
        if name.startswith(kept):
            g.materials=[SKIN]*len(g.faces)
            yield name,g


def part_id(tag,slot):
    if tag==GUARD_VARIANT and slot!="Helmet":
        tag="Guard"
    kit=KITS.get(tag)
    color=kit.skin if kit else "skin"
    profile=kit.profile if kit else tag
    style=kit.hair if kit else "crop"
    sex=profile.split("_")[0]
    if slot=="Head":
        return f"Sheet_Shared_Head_{sex}_{color}"
    if slot=="Neck" and kit:
        return f"Sheet_{tag}_{slot}"
    if slot=="Neck" or (slot.startswith("Foot_") and FIGURES[profile].gap==1.):
        return f"Sheet_Shared_{slot}_{color}"
    if slot.startswith("Hand_"):
        finish="Glove" if tag in ("Explorer","Guard","Pilot","NPC_Miner","NPC_HeavyCombat") else color
        return f"Sheet_Shared_{slot}_{finish}"
    if slot.startswith("Boot_"):
        finish="Dark" if tag in ("Guard","NPC_Miner","NPC_HeavyCombat") else "Sand"
        return f"Sheet_Shared_{slot}_{finish}_{profile}"
    if slot=="Hair":
        return f"Sheet_Shared_Hair_{style}"
    if slot=="Face":
        return f"Sheet_Shared_Face_{sex}_{color}"
    return "Sheet_"+tag+"_"+slot


def clone_actor(scene,bank,rig,parts,tag,offset):
    actor=rig.copy()
    actor.name="Review_"+tag
    actor.animation_data_clear()
    scene.collection.objects.link(actor)
    actor.location=offset
    for id in parts:
        prototype=bank[id]
        obj=prototype.copy()
        obj.name="Review_"+prototype.name
        obj.parent=actor
        obj.modifiers[0].object=actor
        scene.collection.objects.link(obj)
    bpy.context.view_layer.update()
    for side,sign in (("L",1),("R",-1)):
        bone=actor.pose.bones["upper_arm."+side]
        orient=bone.matrix.to_quaternion()
        bone.rotation_mode="QUATERNION"
        bone.rotation_quaternion=orient.inverted() @ Quaternion((0,1,0),sign*.28) @ orient
    return actor


def flat_material(name, rgb):
    material=bpy.data.materials.get(name)
    if material is None:
        material=bpy.data.materials.new(name)
        material.use_nodes=True
        material.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value=(*linear(rgb),1)
        material.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value=.88
    return material


def studio(name, labels, camera_position, scale):
    scene=bpy.data.scenes.new(name)
    bpy.context.window.scene=scene
    scene.unit_settings.system="METRIC"
    scene.render.engine="CYCLES"
    scene.cycles.device="CPU"
    scene.cycles.samples=24
    scene.cycles.use_denoising=True
    scene.render.resolution_x=1590
    scene.render.resolution_y=650
    scene.render.resolution_percentage=100
    scene.render.image_settings.file_format="PNG"
    scene.view_settings.view_transform="AgX"
    camera=bpy.data.objects.new(name+" Camera",bpy.data.cameras.new(name))
    scene.collection.objects.link(camera)
    camera.location=camera_position
    camera.rotation_euler=(Vector((0,0,.91))-camera.location).to_track_quat("-Z","Y").to_euler()
    camera.data.type="ORTHO"
    camera.data.ortho_scale=scale
    scene.camera=camera
    world=bpy.data.worlds.new(name+" World")
    world.use_nodes=True
    world.node_tree.nodes["Background"].inputs[0].default_value=(.41,.40,.38,1)
    world.node_tree.nodes["Background"].inputs[1].default_value=.42
    scene.world=world
    for key,point,power,color,size in (
        ("Key",(-3,-4,5),720,(1,.89,.79),4),
        ("Fill",(4,-2,3),280,(.80,.89,1),5),
        ("Back",(-.4,5,4),1050,(1,.91,.78),6),
    ):
        data=bpy.data.lights.new(name+" "+key,"AREA")
        data.energy=power
        data.color=color
        data.shape="DISK"
        data.size=size
        lamp=bpy.data.objects.new(key,data)
        scene.collection.objects.link(lamp)
        lamp.location=point
        lamp.rotation_euler=(Vector((0,0,1))-lamp.location).to_track_quat("-Z","Y").to_euler()
    mesh=bpy.data.meshes.new(name+" Floor")
    mesh.from_pydata([(-100,-100,-.005),(100,-100,-.005),(100,100,-.005),(-100,100,-.005)],[],[(0,1,2,3)])
    plane=bpy.data.objects.new("Backdrop",mesh)
    scene.collection.objects.link(plane)
    mesh.materials.append(flat_material("Sheet Preview Backdrop","948578"))
    for label,x,y in labels:
        data=bpy.data.curves.new(name+" "+label,"FONT")
        data.body=label
        data.align_x="CENTER"
        data.size=.080
        obj=bpy.data.objects.new(label,data)
        scene.collection.objects.link(obj)
        obj.location=(x,y,.012)
        obj.rotation_euler=(math.pi/2,0,0 if y<0 else math.pi)
        data.materials.append(flat_material("Sheet Preview Labels","eadfce"))
    return scene


def view(name,entries,bank,rig,render,back=False,turn=0,camera=None):
    width=max(5.75,len(entries)*1.05)
    direction=-1 if back else 1
    xs=[direction*(i-(len(entries)-1)/2)*.95 for i in range(len(entries))]
    labels=[(entry[0],x,-direction*.26) for entry,x in zip(entries,xs)]
    scene=studio(name,labels,camera or (1.02,-12*direction,3.66),width)
    for entry,x in zip(entries,xs):
        _,tag,ids=entry[:3]
        actor=clone_actor(scene,bank,rig,ids,tag,(x,0,0))
        actor.rotation_euler.z=entry[3] if len(entry)>3 else turn
    if render:
        scene.render.filepath=str(PREVIEWS/(name+".png"))
        bpy.ops.render.render(write_still=True)
    return scene


def face_view(recipes,banks,rig,render):
    name="HumanSheet_Face_Hair"
    entries=[("Guard | LOD0","Guard","LOD0",-.15),("Explorer | LOD0","Explorer","LOD0",.14),
             ("Resident | LOD0","Resident","LOD0",.55),("Explorer | LOD2","Explorer","LOD2",.14)]
    xs=[.305*(i-1.5) for i in range(len(entries))]
    scene=studio(name,[(label,x,-.04) for (label,*_),x in zip(entries,xs)],
                 (0,-5,1.73),1.30)
    scene.camera.rotation_euler=(Vector((0,0,1.68))-scene.camera.location).to_track_quat("-Z","Y").to_euler()
    scene.render.resolution_x=1536
    scene.render.resolution_y=504
    for obj in scene.objects:
        if obj.type=="FONT":
            obj.location.z=1.513
            obj.data.size=.027
    for (_,tag,stage,turn),x in zip(entries,xs):
        ids=[key for key in recipes[tag][stage]["parts"]
             if any("_"+part+"_" in key for part in ("Head","Hair","Face")) or key.endswith("_Neck")]
        actor=clone_actor(scene,banks[stage],rig,ids,tag,(x,0,0))
        actor.rotation_euler.z=turn
    if render:
        scene.render.filepath=str(PREVIEWS/(name+".png"))
        bpy.ops.render.render(write_still=True)
    return scene


def shared_gear_view(bank,render):
    name="HumanSheet_Shared_Gear"
    slots=list(SHARED_ACCESSORIES.values())
    positions=[((i%3-1)*.59,1.37-(i//3)*.46) for i in range(len(slots))]
    labels=[(label,x,-.25) for (_,_,label),(x,_) in zip(slots,positions)]
    scene=studio(name,labels,(0,-8,1.40),1.84)
    scene.render.resolution_x=1380
    scene.render.resolution_y=1080
    next(obj for obj in scene.objects if obj.name.startswith("Backdrop")).hide_render=True
    for (tag,slot,label),(x,z) in zip(slots,positions):
        prototype=bank[part_id(tag,slot)]
        lo,hi=(Vector(point) for point in bounds(prototype.data))
        center=(lo+hi)*.5
        gear=bpy.data.objects.new(label,prototype.data)
        scene.collection.objects.link(gear)
        turn=Quaternion((0,0,1),-.33 if slot.endswith("_R") else .34)
        gear.rotation_mode="QUATERNION"
        gear.rotation_quaternion=turn
        gear.location=Vector((x,0,z))-turn@center
        text=next(obj for obj in scene.objects if obj.type=="FONT" and obj.data.body==label)
        text.location.z=z-.214
        text.data.size=.039
    if render:
        scene.render.filepath=str(PREVIEWS/(name+".png"))
        bpy.ops.render.render(write_still=True)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    library=bpy.context.scene
    library.name="HumanSheet_Part_Library"
    library.unit_settings.system="METRIC"
    master=ROOT/"Source/Erma_Resource_Library.blend"
    with bpy.data.libraries.load(str(master),link=False) as (source,target):
        assert "HUMAN_7H" in source.objects
        target.objects=["HUMAN_7H"]
    rig=target.objects[0]
    rig.animation_data_clear()
    library.collection.objects.link(rig)
    for bone in rig.pose.bones:
        bone.matrix_basis.identity()
    assert len(rig.data.bones)==23

    paths=write_maps(TEXTURES)
    paths["Equipment"]=write_equipment_atlas(TEXTURES)
    materials=pbr_materials(paths)
    tints={key:linear(value) for key,value in PALETTE.items()}
    source=bpy.data.scenes.new("HumanSheet_Detail_Source")
    source.unit_settings.system="METRIC"
    source.collection.objects.link(rig)
    figures=(*KITS,GUARD_VARIANT,*FIGURES)
    detail={}
    shared=bpy.data.collections.new("Detail_Shared")
    source.collection.children.link(shared)
    for tag in figures:
        group=bpy.data.collections.new("Detail_"+tag)
        source.collection.children.link(group)
        for slot,g in wardrobe(tag,-1,rig.data):
            id=part_id(tag,slot)
            if id in detail:
                continue
            collection=shared if id.startswith("Sheet_Shared_") else group
            obj=make_part(id,"Detail",g,rig,collection,materials,tints=tints,uv_scale=5.5)
            if obj:
                detail[id]=len(obj.data.loop_triangles)
        group.hide_viewport=True
        group.hide_render=True
    shared.hide_viewport=True
    shared.hide_render=True

    banks={stage:{} for stage in STAGES}
    parts={stage:{} for stage in STAGES}
    recipes={tag:{} for tag in figures}
    groups=[]
    for level,stage in enumerate(STAGES):
        group=bpy.data.collections.new(stage)
        library.collection.children.link(group)
        groups.append(group)
        for tag in figures:
            output=shadow_wardrobe(tag,rig.data) if stage=="ShadowProxy" else wardrobe(tag,level,rig.data)
            ids=[]
            for slot,g in output:
                id=part_id(tag,slot)
                if id in banks[stage]:
                    ids.append(id)
                    continue
                obj=make_part(id,stage,g,rig,group,materials,tints=tints,uv_scale=5.5)
                if obj is None:
                    continue
                ids.append(id)
                banks[stage][id]=obj
                parts[stage][id]={"node":obj.name,"triangles":len(obj.data.loop_triangles),
                                  "cap":obj["triangle_budget"],"bounds_m":bounds(obj.data),
                                  "materials":[m.name for m in obj.data.materials]}
            total=sum(parts[stage][id]["triangles"] for id in ids)
            hero=tag in ("Explorer","Player_Explorer","Ria_Engineer")
            limits=(18000,8000,2500,2200) if hero else (12000,5500,2000,1800)
            assert 0<total<=limits[level],(tag,stage,total)
            recipes[tag][stage]={"parts":ids,"triangles":total,"cap":limits[level]}
    print("HUMAN_SHEET_STAGE_COUNTS",{stage:len(bank) for stage,bank in banks.items()},flush=True)
    EXPORT.parent.mkdir(parents=True,exist_ok=True)
    for obj in library.objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active=rig
    bpy.ops.export_scene.gltf(filepath=str(EXPORT),export_format="GLB",use_active_scene=True,use_selection=True,
                            export_skins=True,export_def_bones=False,export_animations=False,export_extras=True,
                            export_texcoords=True,export_normals=True,export_tangents=True,export_vertex_color="ACTIVE")

    PREVIEWS.mkdir(parents=True,exist_ok=True)
    role_labels={"Explorer":"EXPLORER","Guard":"GUARD","Medic":"MEDIC","Pilot":"PILOT","Resident":"PORT RESIDENT"}
    roles=[(label,tag,recipes[tag]["LOD0"]["parts"]) for tag,label in role_labels.items()]
    added_labels=("MALE EXPLORER","RIA ENGINEER","RESEARCHER","MINER","HEAVY EXOSUIT","HELMET GUARD")
    added=[(label,tag,recipes[tag]["LOD0"]["parts"]) for tag,label in zip(NEW_CHARACTERS,added_labels)]
    neutrals=[(tag.replace("_"," ").upper(),tag,recipes[tag]["LOD0"]["parts"]) for tag in FIGURES]
    lod_preview=[(stage.upper(),stage,recipes["Explorer"][stage]["parts"]) for stage in STAGES]
    bank_lods={}
    for stage in STAGES:
        bank_lods.update({(stage,id):obj for id,obj in banks[stage].items()})
    lod_entries=[(label,key,[(key,id) for id in ids]) for label,key,ids in lod_preview]
    render="--render" in sys.argv
    default=view("HumanSheet_Front",roles,banks["LOD0"],rig,render)
    view("HumanSheet_Back",roles,banks["LOD0"],rig,render,back=True)
    view("HumanSheet_Profile",roles,banks["LOD0"],rig,render,turn=math.pi*.49)
    view("HumanSheet_Bodies",neutrals,banks["LOD0"],rig,render,camera=(0,-12,1.32))
    view("HumanSheet_New_Outfits",added,banks["LOD0"],rig,render,camera=(0,-12,2.7))
    view("HumanSheet_New_Outfits_Back",added,banks["LOD0"],rig,render,back=True)
    shared_gear_view(banks["LOD0"],render)
    anatomy=[
        (label+" "+view_name,tag,recipes[tag]["LOD0"]["parts"],turn)
        for label,tag in (("MALE","Male_Normal"),("FEMALE","Female_Normal"))
        for view_name,turn in (("FRONT",0),("SIDE",math.pi*.49),("BACK",math.pi))
    ]
    view("HumanSheet_Anatomy",anatomy,banks["LOD0"],rig,render)
    view("HumanSheet_LODs",lod_entries,bank_lods,rig,render)
    face_view(recipes,banks,rig,render)
    for group in groups:
        group.hide_render=True
        group.hide_viewport=True
    bpy.context.window.scene=default
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=="VIEW_3D":
                area.spaces.active.region_3d.view_perspective="CAMERA"
                area.spaces.active.overlay.show_overlays=False
                area.spaces.active.shading.color_type="VERTEX"
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND),compress=True)
    image_names=["Man_Default.png","Women_Default.png","Shared_Character_Parts.png",*(kit.sheet for kit in KITS.values())]
    manifest={"schema":"erma.human_drawing_only.v4","date":"2026-11-24","source":BLEND.relative_to(ROOT).as_posix(),
              "source_sha256":digest(BLEND),"part_bank":EXPORT.relative_to(ROOT).as_posix(),
              "part_bank_sha256":digest(EXPORT),"rig":"HUMAN_7H","bone_count":23,
              "scale_m":1.8,"forward":"-Y in Blender; +Z in glTF",
              "design_basis":"Clothing and gear use drawn concept views; anatomy and face use the delivered turntable left and middle figures.",
              "fit_basis":"New proportions scaled visually to 1.80 m in the common animation bind space.",
              "body_proportion_basis":{"length":"shared 1.80 m longitudinal landmarks and HUMAN_7H bind",
                  "male":"Male_Heavy","female":"Female_Heavy",
                  "narrower_figures":"reduce torso/limb girth, shoulder width and medial leg clearance",
                  "female_shoulders":"shorter clavicle silhouette with upper-arm mesh moved inward toward the torso"},
              "shared_accessory_sheet_parts":{ref:part_id(tag,slot)
                                               for ref,(tag,slot,label) in SHARED_ACCESSORIES.items()},
              "anatomy_reference":{"kind":"user turntable GIF","panels":["left male","middle female"],
                                   "sha256":"86dcdc7008277b160233c4e5b849a65f8aab54175df7038784df31989b0d4da9"},
              "hair_lods":{"LOD0":"alpha-masked hair cards over a scalp mesh",
                           "LOD1":"opaque scalp and coarse silhouette", "LOD2":"opaque low-poly silhouette",
                           "ShadowProxy":"opaque silhouette", "atlas_max_px":1024},
              "reference_images":{name:digest(ROOT/"Reference/Characters"/name) for name in image_names},
              "texture_max_px":1024,"textures":{key:{kind:{"file":file.relative_to(ROOT).as_posix(),
                                                            "sha256":digest(file)}
                                                      for kind,file in items.items()} for key,items in paths.items()},
              "parts":parts,"detail_source_triangles":detail,"assemblies":recipes,
              "validation_scope":"Offline asset pack. v4 loader integration pending."}
    MANIFEST.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


if __name__=="__main__":
    from build_human_sheet_layers import main as build_layered
    build_layered()
