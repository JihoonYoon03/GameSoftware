"""Build shared v4 body, outfit and gear meshes using the supplied reference sheets."""
import ast
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
from human_v4_geometry import (EQUIPMENT, ROLES, Geometry, SKIN, boot, equipment, face,
                               hair, hand, head, limb, neck, outer, pelvis, torso)

ROOT=Path(__file__).resolve().parents[1]
MASTER=ROOT/"Source/Erma_Resource_Library.blend"
PROJECT=ROOT/"Source/Human_Reference_Remodel_v4.blend"
EXPORT=ROOT/"Export/HumanV4/HUMAN_7H_Parts.glb"
PREVIEW=ROOT/"Preview/HumanV4"
STAGES=("LOD0","LOD1","LOD2","ShadowProxy")
TINTS={
    "skin":(.53,.318,.225),"ivory":(.80,.77,.65),"bone":(.66,.55,.39),"sand":(.53,.385,.242),
    "amber":(.95,.31,.025),"ochre":(.54,.258,.048),"signal":(.64,.13,.07),"ice":(.11,.52,.73),
    "coal":(.05,.064,.084),"slate":(.073,.12,.136),"blueplate":(.125,.204,.318),"olive":(.15,.183,.133),
    "flight":(.052,.10,.211),"twill":(.225,.168,.118),"plum":(.176,.092,.15),"rubber":(.036,.041,.046),
    "hair":(.063,.035,.025),"rustHair":(.20,.084,.044),"lip":(.23,.094,.074),"glass":(.25,.43,.53),
}
ROLE_SHEETS={
    "Explorer":"Human_Explorer.png","Guard":"Protective_Service_Guard.png","Medic":"Field_Medic.png",
    "Pilot":"Shuttle_Pilot.png","Civilian":"Port_Resident.png",
}
SKIN_PALETTES={
    "Skin_Warm":(.56,.31,.20),
    "Skin_Deep":(.24,.105,.07),
    "Skin_Light":(.83,.58,.40),
    "Skin_Cool":(.45,.29,.24),
}
REVIEW_ACTIONS={
    "WALK":"Walk",
    "AIM":"Aim_Base",
    "RIDE":"Ride_Base",
    "DODGE":"Dodge_Back_L",
}
SHADOW_EQUIPMENT={"Accessory_LifeSupport_TwinTank","Accessory_Research_SampleRack",
                  "Accessory_EVA_Helmet","Accessory_Heavy_Helmet","Helmet_Guard",
                  "Accessory_Mining_Hardhat","Accessory_Mining_PowerRig"}


def read(name):
    return json.loads((ROOT/"Documentation"/name).read_text(encoding="utf-8"))


def write(path,data):
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def helpers(*names):
    path=ROOT/"Tools/build_resource_pack.py"
    tree=ast.parse(path.read_text(encoding="utf-8"))
    selected=[node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in names]
    exec(compile(ast.Module(body=selected,type_ignores=[]),str(path),"exec"),globals())


def material_library():
    materials={}
    for name,texture,roughness,metal in (
        ("Fabric","Pilot_TechnicalWeave",.80,0),
        ("Shell","Pilot_PaintedAlloy",.46,0),
        ("Skin",None,.60,0),
        ("Visor",None,.25,.16),
    ):
        m=bpy.data.materials.new("HumanV4_"+name)
        m.use_nodes=True
        m.diffuse_color=(.32,.34,.36,1)
        nodes,links=m.node_tree.nodes,m.node_tree.links
        shader=nodes.get("Principled BSDF")
        shader.inputs["Roughness"].default_value=roughness
        shader.inputs["Metallic"].default_value=metal
        tint=nodes.new("ShaderNodeVertexColor")
        tint.layer_name="HumanTint"
        tint.location=(-540,150)
        color=tint.outputs["Color"]
        if texture:
            def image(channel,position):
                file=ROOT/f"Export/Textures/Pilot/{texture}_{channel}.png"
                img=bpy.data.images.load(str(file),check_existing=True)
                img.colorspace_settings.name="sRGB" if channel=="BaseColor" else "Non-Color"
                node=nodes.new("ShaderNodeTexImage")
                node.image=img
                node.extension="REPEAT"
                node.location=position
                return node

            base=image("BaseColor",(-800,470))
            mix=nodes.new("ShaderNodeMixRGB")
            mix.blend_type="MULTIPLY"
            mix.inputs[0].default_value=1
            links.new(base.outputs["Color"],mix.inputs[1])
            links.new(color,mix.inputs[2])
            mix.location=(-280,280)
            color=mix.outputs[0]
            normal=image("Normal",(-800,-40))
            normal_map=nodes.new("ShaderNodeNormalMap")
            normal_map.uv_map="UVMap"
            normal_map.inputs["Strength"].default_value=.45 if name=="Fabric" else .32
            normal_map.location=(-250,-20)
            links.new(normal.outputs["Color"],normal_map.inputs["Color"])
            links.new(normal_map.outputs[0],shader.inputs["Normal"])
            orm=image("ORM",(-800,-320))
            channels=nodes.new("ShaderNodeSeparateColor")
            channels.location=(-250,-250)
            links.new(orm.outputs["Color"],channels.inputs[0])
            links.new(channels.outputs["Green"],shader.inputs["Roughness"])
            links.new(channels.outputs["Blue"],shader.inputs["Metallic"])
            # glTF exporter recognises the shared occlusion socket on this node group.
            group=bpy.data.node_groups.get("glTF Material Output")
            if not group:
                group=bpy.data.node_groups.new("glTF Material Output","ShaderNodeTree")
                group.interface.new_socket(name="Occlusion",in_out="INPUT",socket_type="NodeSocketFloat")
            gltf=nodes.new("ShaderNodeGroup")
            gltf.node_tree=group
            gltf.location=(-245,-450)
            links.new(channels.outputs["Red"],gltf.inputs["Occlusion"])
        links.new(color,shader.inputs["Base Color"])
        if name=="Visor":
            shader.inputs["Alpha"].default_value=.30
            m.surface_render_method="BLENDED"
        materials[name]=m
    return materials


def bounds(mesh):
    return [[min(v.co[i] for v in mesh.vertices) for i in range(3)],
            [max(v.co[i] for v in mesh.vertices) for i in range(3)]]


def cap(diagonal,stage):
    if stage=="ShadowProxy":
        return 150
    size=0 if diagonal<.5 else 1 if diagonal<2 else 2 if diagonal<8 else 3 if diagonal<25 else 4
    # Sub-0.5m LOD2 geometry is retained only where it closes a character silhouette.
    return ((400,800,1500,3000,5000),(200,400,750,1500,2500),(150,150,300,600,1000))[STAGES.index(stage)][size]


def make_part(pid,stage,g,rig,collection,materials,*,tints=TINTS,uv_scale=.25,part_limit=True):
    if not g.faces:
        return None
    node=stage+"__"+pid
    mesh=bpy.data.meshes.new(node)
    faces=[]
    finishes=[]
    for index,(indices,surface,smooth) in enumerate(zip(g.faces,g.materials,g.smooth)):
        corners=g.face_uv.get(index)
        items=[tuple(range(len(indices)))] if len(indices)<=4 else [
            (0,i,i+1) for i in range(1,len(indices)-1)]
        for item in items:
            faces.append(tuple(indices[i] for i in item))
            finishes.append((surface,smooth,None if corners is None else tuple(corners[i] for i in item)))
    mesh.from_pydata(g.v,[],faces)
    mesh.update()
    assert not mesh.validate(),node
    slots=list(dict.fromkeys(surface.slot for surface in g.materials))
    for slot in slots:
        mesh.materials.append(materials[slot])
    visibility=getattr(g,"visibility",None)
    if visibility is not None:
        attr=mesh.attributes.new("_BODY_REGION","FLOAT","POINT")
        attr.data.foreach_set("value",visibility)
    mesh.uv_layers.new(name="UVMap")
    mesh.color_attributes.new(name="HumanTint",type="BYTE_COLOR",domain="CORNER")
    uv=mesh.uv_layers["UVMap"]
    color=mesh.color_attributes["HumanTint"]
    mesh.color_attributes.active_color=color
    for polygon,(surface,smooth,corners) in zip(mesh.polygons,finishes):
        polygon.material_index=slots.index(surface.slot)
        polygon.use_smooth=smooth
        k=max(range(3),key=lambda i:abs(polygon.normal[i]))
        u,v=((1,2),(0,2),(0,1))[k]
        handed=1 if polygon.normal[k]>0 else -1
        for corner,point in enumerate(polygon.loop_indices):
            index=mesh.loops[point].vertex_index
            co=mesh.vertices[index].co
            uv.data[point].uv=(corners[corner] if corners is not None else
                               g.vertex_uv.get(index,(co[u]*handed*uv_scale,co[v]*uv_scale)))
            color.data[point].color=(*tints[surface.tint],1)
    if g.seam_normals:
        mesh.normals_split_custom_set_from_vertices(
            [g.seam_normals.get(i,(0,0,0)) for i in range(len(mesh.vertices))])
    obj=bpy.data.objects.new(node,mesh)
    collection.objects.link(obj)
    groups={}
    if rig is not None:
        obj.parent=rig
        mod=obj.modifiers.new("HUMAN_7H","ARMATURE")
        mod.object=rig
        for index,weights in enumerate(g.weights):
            assert abs(sum(weights.values())-1)<1e-6, (node,index,weights)
            for bone,weight in weights.items():
                assert bone in rig.data.bones
                if bone not in groups:
                    groups[bone]=obj.vertex_groups.new(name=bone)
                groups[bone].add([index],weight,"REPLACE")
    mesh.calc_loop_triangles()
    obj["asset_id"]=pid
    obj["lod"]=stage
    if stage in STAGES and part_limit:
        box=bounds(mesh)
        limit=cap((Vector(box[1])-Vector(box[0])).length,stage)
        assert len(mesh.loop_triangles)<=limit,(node,len(mesh.loop_triangles),limit)
        obj["triangle_budget"]=limit
    return obj


def bare_foot(side,lod,detail=False):
    x=.115 if side=="L" else -.115
    return Geometry(detail).loft([(x,-.065,.001),(x,-.067,.035),(x,-.034,.076),(x,0,.137),(x,0,.148)],
                           [(.052,.115),(.053,.114),(.052,.101),(.033,.040),(.031,.034)],
                           (12,9,7)[lod],SKIN,{"foot."+side:1},power=.80)


def iterate_models(rig, profiles, catalog, sockets, level, detail=False):
    for profile,p in profiles.items():
        yield "Body_"+profile+"_Chest",torso(p,level,region="chest",detail=detail)
        yield "Body_"+profile+"_Abdomen",torso(p,level,region="abdomen",detail=detail)
        yield "Body_"+profile+"_Pelvis",pelvis(p,level,detail)
        for role in ROLES:
            prefix=f"Outfit_{role}_{profile}_"
            yield prefix+"Torso",torso(p,level,role,detail=detail)
            yield prefix+"Torso_OuterAssembly",outer(p,role,level,detail)
        for side in ("L","R"):
            for region,slot in zip(("UpperArm","Forearm","Thigh","Shin"),("Sleeve","Cuff","Trouser","Greave")):
                yield f"Body_{profile}_{region}_{side}",limb(rig.data,p,region,side,level,detail=detail)
                for role in ROLES:
                    yield f"Outfit_{role}_{profile}_{slot}_{side}",limb(rig.data,p,region,side,level,role,detail)
    for kind in ("Oval","Angular","Round","Narrow"):
        yield "Head_"+kind,head(kind,level,detail)
    for kind in ("Alert","Neutral","Stern"):
        yield "Face_"+kind,face(kind,level,detail)
    for style in ("Buzz","SidePart","Curls","Bob","Ponytail","Bun","HatClearance_Buzz"):
        yield "Hair_"+style,hair(style,level,detail)
    for side in ("L","R"):
        yield "Body_Hand_"+side,hand(side,level,detail)
        yield "Body_Foot_"+side,bare_foot(side,level,detail)
        yield "Boot_"+side,boot(side,level,detail)
    yield "Body_Neck",neck(level,detail)
    for pid in EQUIPMENT:
        row=catalog[pid]
        box=row["render_bounds"]
        lo,hi=(Vector(box[key]) for key in ("min","max"))
        socket="head" if row["slot"]=="hat" else row["slot"].removeprefix("accessory.")
        bone=sockets["sockets"][socket]["bone"]
        yield pid,equipment(pid,lo,hi,bone,level,detail)


def make_proxy(profile,p,rig):
    g=Geometry()
    shadow=SKIN
    g.loft([(0,0,.93),(0,0,1.10),(0,0,1.40),(0,0,1.48)],
           [(p["hip"]*.85,p["back"]),(p["waist"],p["back"]),(p["shoulder"],p["front"]),(.075,.069)],
           8,shadow,[{"pelvis":1},{"spine":1},{"chest":1},{"chest":1}])
    g.loft([(0,0,1.446),(0,0,1.494),(0,0,1.583)],
           [(.066,.062),(.056,.054),(.052,.058)],6,shadow,
           [{"chest":1},{"neck":1},{"head":.38,"neck":.62}])
    result=dict([
        ("Body_"+profile+"_ShadowCore",g),
        ("Head_Oval",head("Oval",2)),
        *((f"Body_{profile}_{key}_{s}",limb(rig.data,p,key,s,2))
          for key in ("UpperArm","Forearm","Thigh","Shin") for s in ("L","R")),
        *((f"Body_Hand_{s}",hand(s,2)) for s in ("L","R")),
        *((f"Boot_{s}",boot(s,2)) for s in ("L","R")),
    ])
    for part in result.values():
        part.materials=[shadow]*len(part.faces)
    return result


def make_recipe(name, info, parts, proxies, catalog, fitting):
    old=info["parts"]
    base=[]
    for pid in old:
        if pid in parts["LOD0"]:
            base.append(pid)
    if name=="Reference_Explorer_Female":
        base=[pid.replace("Male_Normal","Female_Normal").replace("Hair_SidePart","Hair_Bun")
              .replace("Accessory_Wrist_Scanner_L","Accessory_Wrist_Scanner_R") for pid in base]
    body=info["body"]
    # A common torso must cover the original pelvis seam in all six body fittings.
    stage_data={}
    offsets=(fitting.get(body,{}).get(info["role"],{}) if name=="Reference_Explorer_Female"
             else info.get("offsets",{}))
    for stage in STAGES:
        ids=[pid for pid in base if pid in parts[stage]]
        if stage=="ShadowProxy":
            ids=proxies[body]+[pid for pid in ids if pid in SHADOW_EQUIPMENT]
        stage_data[stage]=[{"part_id":pid,"node":parts[stage][pid].name,
                           "offset_m":offsets.get(pid,[0,0,0])} for pid in ids]
    stage_cap=[18000,8000,2500,2200] if name in ("Player_Explorer","Ria_Engineer","Mara_Medic","Kain_Guard","Reference_Explorer_Female") else [12000,5500,2000,1800]
    reused=[pid for pid in old if pid not in parts["LOD0"]]
    totals={}
    for stage,limit in zip(STAGES,stage_cap):
        count=sum(len(parts[stage][row["part_id"]].data.loop_triangles) for row in stage_data[stage])
        optional=sum(catalog[pid]["triangles"] for pid in reused) if stage in ("LOD0","LOD1") else 0
        assert 0<count<=count+optional<=limit,(name,stage,count,optional,limit)
        totals[stage]={"triangles":count,"optional_v3_triangles":optional,"cap":limit}
    return {"role":info["role"],"body":body,"skin_palette":info.get("skin") or "Skin_Warm",
            "stages":stage_data,"totals":totals,
            "optional_v3_parts":reused,
            "reference":ROLE_SHEETS.get(info["role"],"Documentation/CHARACTER_DESIGN_ko.md")}


def copy_actor(col, rig, parts, name, instances, origin, palette=None):
    actor=rig.copy()
    actor.name=name
    actor.location=origin
    actor.animation_data_clear()
    col.objects.link(actor)
    for row in instances:
        obj=parts[row["part_id"]].copy()
        obj.name=name+"__"+row["part_id"]
        obj.parent=actor
        obj.location=row["offset_m"]
        for mod in obj.modifiers:
            if mod.type=="ARMATURE":
                mod.object=actor
        if palette:
            for slot in obj.material_slots:
                if slot.material.name=="HumanV4_Skin":
                    slot.link="OBJECT"
                    slot.material=palette
        col.objects.link(obj)
    return actor


def studio_materials():
    # The original studio helpers use these two labelled materials for ground and text.
    global MATS
    MATS={}
    for name,color in (("Rubber",(.095,.116,.142,1)),("Ceramic",(.91,.95,.97,1))):
        m=bpy.data.materials.new("Review_"+name)
        m.use_nodes=True
        m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value=color
        MATS[name]=m


def preview_skin(palette):
    if not palette:
        return None
    name="Review_"+palette
    m=bpy.data.materials.get(name)
    if not m:
        m=bpy.data.materials.new(name)
        m.use_nodes=True
        shader=m.node_tree.nodes["Principled BSDF"]
        shader.inputs["Base Color"].default_value=(*SKIN_PALETTES[palette],1)
        shader.inputs["Roughness"].default_value=.65
    return m


def review_scene(name,rig,parts,recipes,actors,stage,render,actions=None,camera=(1.65,-13,4.7)):
    scene,col=studio(name,camera,(0,0,.93),max(5.65,len(actors)*1.22),(1240,500))
    bpy.context.window.scene=scene
    ground(col,20,-.004)
    direction=1 if camera[1]<0 else -1
    if actions:
        scene.frame_end=1
    for i,(label,key) in enumerate(actors):
        x=direction*(i-(len(actors)-1)/2)*1.12
        recipe=recipes[key]
        actor=copy_actor(col,rig,parts[stage],label,recipe["stages"][stage],(x,0,0),
                         preview_skin(recipe.get("skin_palette")))
        if actions:
            animation=actor.animation_data_create()
            name_action="HUMAN_7H__"+actions[label]
            animation.action=bpy.data.actions[name_action]
            if animation.action.slots:
                animation.action_slot=animation.action.slots[0]
            actor["review_action"]=name_action
            scene.frame_end=max(scene.frame_end,math.ceil(animation.action.frame_range[1]))
        text_label(col,label,(x,-direction*.31,.008),.095)
    if direction<0:
        for obj in col.objects:
            if obj.type=="FONT":
                obj.rotation_euler.z=math.pi
    scene.cycles.samples=12
    if render:
        if actions:
            scene.frame_set(8)
        scene.render.filepath=str(PREVIEW/(name+".png"))
        bpy.ops.render.render(write_still=True)
        if actions:
            scene.frame_set(1)
    return scene


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene
    scene.name="HUMAN_7H_V4_Part_Library"
    scene.unit_settings.system="METRIC"
    action_names=["HUMAN_7H__"+n for n in ("Idle",*REVIEW_ACTIONS.values())]
    with bpy.data.libraries.load(str(MASTER),link=False) as (source,target):
        assert "HUMAN_7H" in source.objects
        target.objects=["HUMAN_7H"]
        target.actions=[n for n in action_names if n in source.actions]
    rig=bpy.data.objects["HUMAN_7H"]
    scene.collection.objects.link(rig)
    rig.animation_data_clear()
    assert len(rig.data.bones)==23
    print("HUMAN_V4_BIND_LOADED",flush=True)
    for bone in rig.pose.bones:
        bone.matrix_basis.identity()
    materials=material_library()
    profiles=read("body_profiles.json")
    catalog={row["id"]:row for row in read("asset_catalog.json")["assets"]}
    sockets=read("accessory_sockets.json")
    detail_scene=bpy.data.scenes.new("HUMAN_7H_V4_Detail_Source")
    detail_scene.unit_settings.system="METRIC"
    detail_scene.collection.objects.link(rig)
    detail_collection=bpy.data.collections.new("Editable_Detail_Sources")
    detail_scene.collection.children.link(detail_collection)
    detail_scene["instructions"]="Show Editable_Detail_Sources and isolate the Detail asset that matches the LOD0 asset_id."
    source_counts={}
    for pid,g in iterate_models(rig,profiles,catalog,sockets,0,detail=True):
        obj=make_part(pid,"Detail",g,rig,detail_collection,materials)
        source_counts[pid]=len(obj.data.loop_triangles)
    detail_collection.hide_viewport=True
    detail_collection.hide_render=True
    collections={}
    for name in STAGES:
        part_collection=bpy.data.collections.new(name)
        scene.collection.children.link(part_collection)
        collections[name]=part_collection
    parts={name:{} for name in STAGES}
    shadow_parts=parts["ShadowProxy"]
    for li,name in enumerate(STAGES[:3]):
        print("HUMAN_V4_STAGE",name,flush=True)
        for pid,g in iterate_models(rig,profiles,catalog,sockets,li):
            obj=make_part(pid,name,g,rig,collections[name],materials)
            if obj:
                parts[name][pid]=obj
                if name=="LOD2" and pid in SHADOW_EQUIPMENT:
                    g.materials=[SKIN]*len(g.faces)
                    shadow_parts[pid]=make_part(pid,"ShadowProxy",g,rig,collections["ShadowProxy"],materials)
    proxies={}
    for profile,p in profiles.items():
        proxy=make_proxy(profile,p,rig)
        proxies[profile]=list(proxy)
        for pid,g in proxy.items():
            if pid not in shadow_parts:
                shadow_parts[pid]=make_part(pid,"ShadowProxy",g,rig,collections["ShadowProxy"],materials)
    recipe_data=read("assembly_recipes.json")
    recipe_data={n:r for n,r in recipe_data.items() if isinstance(r,dict) and r.get("body") in profiles}
    recipe_data["Reference_Explorer_Female"]={**recipe_data["Player_Explorer"],"body":"Female_Normal"}
    fitting=read("accessory_fitting_offsets.json")
    recipes={name:make_recipe(name,info,parts,proxies,catalog,fitting) for name,info in recipe_data.items()}
    bodies={}
    for profile in profiles:
        ids=[pid for pid in parts["LOD0"] if pid.startswith("Body_"+profile+"_")]
        ids += ["Body_Neck","Head_Oval","Face_Neutral","Hair_Buzz","Body_Hand_L","Body_Hand_R","Body_Foot_L","Body_Foot_R"]
        bodies[profile]={"stages":{stage:[{"part_id":pid,"offset_m":[0,0,0]}
                                          for pid in ids if pid in parts[stage]]
                                    for stage in STAGES[:3]}}
    table={}
    for stage,objs in parts.items():
        table[stage]={pid:{"node":obj.name,"triangles":len(obj.data.loop_triangles),"cap":obj["triangle_budget"],
                          "bounds_m":bounds(obj.data),"materials":[m.name for m in obj.data.materials]}
                      for pid,obj in objs.items()}
    print("HUMAN_V4_STAGE_COUNTS",{k:len(v) for k,v in parts.items()},flush=True)
    assert parts["LOD0"].keys()==source_counts.keys()
    EXPORT.parent.mkdir(parents=True,exist_ok=True)
    for obj in scene.objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active=rig
    bpy.ops.export_scene.gltf(filepath=str(EXPORT),export_format="GLB",use_selection=True,use_active_scene=True,
        export_extras=True,export_animations=False,export_skins=True,export_def_bones=False,export_tangents=True,
        export_texcoords=True,export_normals=True,export_vertex_color="ACTIVE")

    # Editable render scenes are in the .blend. The bank contains only the unique skinned parts.
    PREVIEW.mkdir(parents=True,exist_ok=True)
    studio_materials()
    helpers("studio","ground","text_label")
    render="--render" in sys.argv
    role_examples=(("EXPLORER","Reference_Explorer_Female"),("GUARD","Kain_Guard"),("MEDIC","Mara_Medic"),
                   ("PILOT","NPC_Pilot"),("PORT RESIDENT","NPC_Civilian"))
    default=review_scene("HumanV4_Costume_Review",rig,parts,recipes,role_examples,"LOD0",render)
    review_scene("HumanV4_Equipment_Review",rig,parts,recipes,role_examples,"LOD0",render,
                 camera=(3.85,13,4.8))
    review_scene("HumanV4_Body_Review",rig,parts,bodies,
                 tuple((p.replace("_"," ").upper(),p) for p in profiles),"LOD0",render)
    review_scene("HumanV4_Other_Outfits",rig,parts,recipes,
                 (("ENGINEER","Ria_Engineer"),("RESEARCHER","NPC_Researcher"),("MINER","NPC_Miner"),
                  ("HEAVY COMBAT","NPC_HeavyCombat")),"LOD0",render)
    review_scene("HumanV4_Deformation_Review",rig,parts,recipes,
                 (("WALK","Reference_Explorer_Female"),("AIM","NPC_Guard_Helmet"),("RIDE","NPC_Pilot"),
                  ("DODGE","NPC_HeavyCombat")),"LOD0",render,REVIEW_ACTIONS)
    lod_recipes={stage:{"stages":{"LOD0":recipes["Reference_Explorer_Female"]["stages"][stage]},
                        "skin_palette":recipes["Reference_Explorer_Female"]["skin_palette"]}
                 for stage in STAGES}
    flattened={obj.name:obj for stage in STAGES for obj in parts[stage].values()}
    for stage in STAGES:
        lod_recipes[stage]["stages"]["LOD0"]=[{**row,"part_id":row["node"]}
                                             for row in lod_recipes[stage]["stages"]["LOD0"]]
    review_scene("HumanV4_LOD_Review",rig,{"LOD0":flattened},lod_recipes,
                 tuple((stage.upper(),stage) for stage in STAGES),"LOD0",render)
    for action in bpy.data.actions:
        if action.name in action_names:
            action.use_fake_user=True
    for group in collections.values():
        group.hide_render=True
        group.hide_viewport=True
    scene["instructions"]="Show one LOD collection and isolate the asset_id you are editing. Use the named review scenes to see fitted human assemblies."
    bpy.context.window.scene=default
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=="VIEW_3D":
                area.spaces.active.region_3d.view_perspective="CAMERA"
                area.spaces.active.overlay.show_overlays=False
                area.spaces.active.shading.color_type="VERTEX"
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(PROJECT),compress=True)
    refs={image:file_hash(ROOT/"Reference/Characters"/image) for image in
          sorted({"Man_Default.png","Women_Default.png",*ROLE_SHEETS.values()})}
    write(ROOT/"Documentation/human_reference_v4.json",{
        "schema":"erma.human_authoring.v4", "date":"2026-11-24",
        "source":PROJECT.relative_to(ROOT).as_posix(),"source_sha256":file_hash(PROJECT),
        "detail_source_scene":detail_scene.name,"detail_source_triangles":source_counts,
        "shared_part_bank":EXPORT.relative_to(ROOT).as_posix(),
        "shared_part_bank_sha256":file_hash(EXPORT),"rig":"HUMAN_7H","bone_count":23,
        "units":"metres; Blender +X left, -Y forward, +Z up; glTF (x,z,-y)",
        "axis_and_fit":"Use the reference drawings for silhouettes, with all bind-space fitting from the current rig and equipment dimensions.",
        "reference_checksums":refs,"part_stages":table,"characters":recipes,"neutral_bodies":bodies,
        "remodeled_equipment_ids":list(EQUIPMENT),
        "outfit_variants":{"roles":list(ROLES),"body_profiles":list(profiles)},
        "material_contract":{"tint_attribute":"COLOR_0, linear RGB","sampler":"repeat",
            "opaques":["Fabric","Shell","Skin"],"separate_transparent":"Visor",
            "skin_palette_linear_rgb":SKIN_PALETTES,
            "skin_palette_scope":"Use the character palette in place of COLOR_0 on HumanV4_Skin only.",
            "shared_maps":{"Fabric":"Pilot_TechnicalWeave","Shell":"Pilot_PaintedAlloy"},
            "normal":"tangent space +Y","ORM":"linear R=AO G=roughness B=metallic"},
        "common_animation_sources":"existing 98 base clips and separately delivered extensions",
        "deformation_review_scene":"HumanV4_Deformation_Review",
        "integration":"v4 authoring delivery; game loader/LOD selection integration pending",
    })


if __name__=="__main__":
    main()
