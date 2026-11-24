"""Bake the authoring anatomy, transferred wardrobe and rigid socket bank into HumanSheet."""
import json
import math
from pathlib import Path
import sys
from collections import defaultdict

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
from build_human_reference_v4 import bounds, make_part
from build_human_sheet_v4 import (
    BLEND, EXPORT, MANIFEST, PALETTE, PREVIEWS, ROOT, SHARED_ACCESSORIES, STAGES, TEXTURES,
    digest, linear, pbr_materials, studio, wardrobe,
)
from human_layered_geometry import bare_body, face_components, fitted_suit, merge
from human_equipment_atlas import write_equipment_atlas
from human_guard_armor import guard_parts
from human_sheet_additions import GUARD_VARIANT, NEW_CHARACTERS
from human_sheet_forms import FIGURES, KITS, SKIN, hair
from human_sheet_maps import write_maps
from verify_human_reference_v4 import skeleton
from verify_resource_pack import read_glb


ROLES=(*KITS,GUARD_VARIANT)
HEROES={"Explorer","Player_Explorer","Ria_Engineer"}
GLOVES={"Explorer","Guard",GUARD_VARIANT,"Pilot","NPC_Miner","NPC_HeavyCombat"}
SEALED={GUARD_VARIANT,"NPC_HeavyCombat"}
SOFT={"Collar","ShoulderStraps","Belt","JacketTrim","FlightHarness"}
SOCKET_BACK={"DualLifeSupport","DorsalPack","RescuePack","FlightSupportPack","CommuterPack",
             "IndustrialPowerRig","DorsalPowerPack","SampleRack"}
STATIC_HEAD={"Helmet","Hardhat","Respirator","SafetyGoggles","ResearchVisor"}
CASTS_FAR=SOCKET_BACK|{"Helmet","Hardhat","Respirator","ChestShell"}
AUTHOR_AXES=Matrix(((0,1,0,0),(-1,0,0,0),(0,0,1,0),(0,0,0,1)))
GLTF_AXES=Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))


def pack_matrix(matrix):
    return [round(matrix[row][column],8) for column in range(4) for row in range(4)]


def collections(scene,stage):
    out={}
    for tier in ("Body","Clothing","SkinAttachment","SocketAttachment"):
        group=bpy.data.collections.new(stage+"_"+tier)
        scene.collection.children.link(group)
        out[tier]=group
    return out


def choose_stage(stage,tag):
    return -1 if stage=="LOD0" and (tag in HEROES or tag in ("Male","Female")) else STAGES.index(stage) if stage!="ShadowProxy" else 2


def kit_for(tag):
    return KITS["Guard" if tag==GUARD_VARIANT else tag]


def body_id(sex):
    return f"Human_{sex}_MasterBody"


def asset_id(tag,slot):
    if tag==GUARD_VARIANT and slot!="Helmet":
        tag="Guard"
    return f"Human_{tag}_{slot}"


def bone_clean(obj):
    """Limit Total (4), Clean (0.01) and Normalize All on the transferred deform groups."""
    groups={index:g for index,g in enumerate(obj.vertex_groups)}
    for v in obj.data.vertices:
        live=sorted(((w.group,w.weight) for w in v.groups),key=lambda e:e[1],reverse=True)[:4]
        chosen={i:w for i,w in live if w>=.01}
        if not chosen:
            assert live, (obj.name,v.index)
            chosen=dict(live[:1])
        total=sum(chosen.values())
        for i in [entry.group for entry in v.groups]:
            if i not in chosen:
                groups[i].remove([v.index])
        for i,w in chosen.items():
            groups[i].add([v.index],w/total,"REPLACE")


def transfer(obj,source):
    bpy.context.view_layer.objects.active=obj
    modifier=obj.modifiers.new("Skin from master (face interpolated)","DATA_TRANSFER")
    modifier.object=source
    modifier.use_vert_data=True
    modifier.data_types_verts={"VGROUP_WEIGHTS"}
    modifier.vert_mapping="POLYINTERP_NEAREST"
    modifier.layers_vgroup_select_src="ALL"
    modifier.layers_vgroup_select_dst="NAME"
    obj.modifiers.move(len(obj.modifiers)-1,0)
    bpy.context.view_layer.update()
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    if obj["asset_id"].endswith("_Coat"):
        soften_coat_weights(obj)
    bone_clean(obj)
    obj["weight_source"]="HUMAN_7H master / Nearest Face Interpolated"


def soften_coat_weights(obj):
    neighbours=defaultdict(list)
    for edge in obj.data.edges:
        a,b=edge.vertices
        neighbours[a].append(b)
        neighbours[b].append(a)
    old=[{g.group:g.weight for g in v.groups} for v in obj.data.vertices]
    for i,adjacent in neighbours.items():
        mixed={k:.65*w for k,w in old[i].items()}
        for j in adjacent:
            for k,w in old[j].items():
                mixed[k]=mixed.get(k,0)+w*.35/len(adjacent)
        for k,w in mixed.items():
            obj.vertex_groups[k].add([i],w,"REPLACE")


def shape_source(sex,stage,rig,group,mats,tints):
    layout={}
    use=-1 if stage=="Detail" else choose_stage(stage,sex)
    shapes={name:bare_body(rig.data,f"{sex}_{name}",use,layout,facial_details=stage!="ShadowProxy")
            for name in ("Normal","Lean","Heavy")}
    basis=shapes["Normal"]
    # Morph targets share index order, including each of the ten welded anatomical joins.
    for name,result in shapes.items():
        assert basis.faces==result.faces and len(basis.v)==len(result.v),(sex,stage,name)
    obj=make_part(body_id(sex),stage,basis,rig,group,mats,tints=tints,uv_scale=3.6,part_limit=False)
    if stage!="Detail":
        obj["layer"]="Body"
        obj["visibility_attribute"]="_BODY_REGION"
    obj.shape_key_add(name="Basis")
    for name in ("Lean","Heavy"):
        key=obj.shape_key_add(name=name)
        for vertex,co in zip(key.data,shapes[name].v):
            vertex.co=co
    bone_clean(obj)
    return obj


def socket_kind(slot,bone):
    if slot in STATIC_HEAD:
        return "head"
    if slot in SOCKET_BACK:
        return "back"
    if slot.startswith("ShoulderCap"):
        return "shoulder_"+slot[-1]
    if slot.startswith(("WristScanner","Cuff")):
        return "forearm_"+slot[-1]
    if slot.startswith(("ToolRoll","WorkLamp")):
        return ("hip_" if slot.startswith("Tool") else "shoulder_")+slot[-1]
    if "Pouch" in slot:
        return "hip_L" if slot.endswith("_L") or slot=="HipPouch" else "hip_R"
    if slot in ("ChestShell","ChestConsole","TriageRack","NeckSeal","HarnessDock"):
        return "chest" if bone=="chest" else "spine"
    if slot in ("BeltBuckles","PelvisPlate","WaistArmor"):
        return "waist_front"
    return bone.replace(".","_")


def socket_data(slot,bone,g,anchors,rig):
    socket=socket_kind(slot,bone)
    row=anchors.get(socket)
    if row:
        pivot=Vector(row["position"])
        if slot.startswith("WorkLamp"):
            # Lamps ride the harness; the cap can rotate with the upper arm independently.
            bone="clavicle."+slot[-1]
        elif slot not in ("ChestShell","ChestConsole","TriageRack","BeltBuckles","PelvisPlate","NeckSeal",
                           "HarnessDock","WaistArmor"):
            bone=row["bone"]
    else:
        pivot=rig.data.bones[bone].head_local.copy()
    matrix=Matrix.Translation(pivot)@AUTHOR_AXES
    inv=matrix.inverted()
    for i,co in enumerate(g.v):
        g.v[i]=inv@co
    for i,n in g.seam_normals.items():
        g.seam_normals[i]=inv.to_3x3()@n
    bind=GLTF_AXES@matrix@GLTF_AXES.inverted()
    return {"socket":socket,"bone":bone,"bind_matrix_blender":pack_matrix(matrix),
            "bind_matrix_gltf":pack_matrix(bind)}


def hard_and_soft(tag,stage,rig,suit):
    if tag in {"Guard",GUARD_VARIANT}:
        parts=guard_parts(stage,rig.data,FIGURES[KITS["Guard"].profile],tag==GUARD_VARIANT,suit)
        if tag=="Guard":
            yield "SkinAttachment","Hair",hair(stage,KITS[tag].hair)
    else:
        parts=wardrobe(tag,stage,rig.data)
    for slot,g in parts:
        if slot in ("Torso","Head","Face","Neck") or slot.startswith(("Arm_","Leg_","Hand_","Boot_","Foot_")):
            continue
        if not g.faces:
            continue
        if slot=="Hair":
            yield "SkinAttachment",slot,g
        elif slot in ("Belt","FlightHarness","Collar","ShoulderStraps"):
            if slot=="Belt":
                chosen=[i for i,face in enumerate(g.faces) if
                        len(face) in (3,4) and all(abs(g.v[k].x)<.042 for k in face)
                        and g.materials[i].slot=="Shell" and g.materials[i].tint in ("black","gunmetal","amber")]
                if chosen:
                    body_faces=[i for i in range(len(g.faces)) if i not in set(chosen)]
                    yield "SocketAttachment","BeltBuckles",extract(g,chosen)
                    g=extract(g,body_faces)
            yield "Clothing",slot,g
        elif slot in SOFT or slot.startswith("CoatTail"):
            yield "Clothing",slot,g
        else:
            yield "SocketAttachment",slot,g


def extract(g,indices):
    from human_v4_geometry import Geometry

    h=Geometry()
    vertices=sorted({v for i in indices for v in g.faces[i]})
    lookup={v:j for j,v in enumerate(vertices)}
    h.v=[g.v[v].copy() for v in vertices]
    h.weights=[g.weights[v] for v in vertices]
    h.vertex_uv={lookup[v]:uv for v,uv in g.vertex_uv.items() if v in lookup}
    for i in indices:
        h.poly((lookup[v] for v in g.faces[i]),g.materials[i],g.smooth[i],g.face_uv.get(i))
    return h


def attach_parts(tag,stage,raw,rig,groups,mats,tints,anchors,masters,bank,rows):
    kit=kit_for(tag)
    sex,build=kit.profile.split("_")
    body=masters[sex]
    source_keys=body.data.shape_keys.key_blocks
    if build!="Normal":
        source_keys[build].value=1
    # A frozen copy of the chosen master morph is used for transfer in rest pose.
    bpy.context.view_layer.update()
    donor_mesh=bpy.data.meshes.new_from_object(body.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    donor=bpy.data.objects.new("__temporary_skin_donor",donor_mesh)
    groups["Clothing"].objects.link(donor)
    for g in body.vertex_groups:
        donor.vertex_groups.new(name=g.name)
    if build!="Normal":
        source_keys[build].value=0
    suit,*_=fitted_suit(rig.data,kit.profile,raw,kit,tag in GLOVES)
    main=[(suit,1,None)]
    outer=[]
    hair=[]
    rigid=[]
    silhouette_prefixes=("ShoulderCap","ExoskeletonStrut") if tag=="NPC_HeavyCombat" else (
        "ShoulderCap","LegPanels")
    for layer,slot,g in hard_and_soft(tag,raw,rig,suit):
        if stage=="ShadowProxy" and slot not in CASTS_FAR and not slot.startswith(("CoatTail","ShoulderCap","Hair")):
            continue
        if stage=="LOD2" and slot in ("Belt","ShoulderStraps"):
            continue
        # Keep small attachments out of the distant character bank.
        if (stage=="LOD2" and layer=="SocketAttachment" and slot not in CASTS_FAR
                and not slot.startswith(silhouette_prefixes)):
            continue
        if layer=="Clothing":
            (outer if slot.startswith("CoatTail") else main).append((g,1,None))
        elif layer=="SkinAttachment":
            hair.append((slot,g))
        else:
            rigid.append((slot,g))
    entries=[]
    if stage=="ShadowProxy":
        main=[]
    for key,pieces in (("Clothing",main),("Coat",outer)):
        if not pieces:
            continue
        g=merge(pieces,weld=True)
        if stage=="ShadowProxy":
            g.materials=[SKIN]*len(g.faces)
        del g.visibility
        asset=asset_id(tag,key)
        if asset not in bank:
            obj=make_part(asset,stage,g,rig,groups["Clothing"],mats,tints=tints,uv_scale=3.6,part_limit=False)
            transfer(obj,donor)
            register(asset,obj,"Clothing",bank,rows)
        entries.append(asset)
    bpy.data.objects.remove(donor,do_unlink=True)
    bpy.data.meshes.remove(donor_mesh)
    for slot,g in hair:
        asset="Human_Shared_Hair_"+kit.hair
        if asset not in bank:
            if stage=="ShadowProxy":
                g.materials=[SKIN]*len(g.faces)
            obj=make_part(asset,stage,g,rig,groups["SkinAttachment"],mats,tints=tints,uv_scale=3.6,part_limit=False)
            bone_clean(obj)
            register(asset,obj,"SkinAttachment",bank,rows)
        entries.append(asset)
    for slot,g in rigid:
        partitions=face_components(g)
        for bone,piece in partitions.items():
            if not piece.faces:
                continue
            suffix=("_"+bone.replace(".","_")) if len(partitions)>1 else ""
            asset=asset_id(tag,slot+suffix)
            if asset not in bank:
                socket=socket_data(slot,bone,piece,anchors,rig)
                if stage=="ShadowProxy":
                    piece.materials=[SKIN]*len(piece.faces)
                obj=make_part(asset,stage,piece,None,groups["SocketAttachment"],mats,tints=tints,
                              uv_scale=3.6,part_limit=False)
                obj["socket"]=socket["socket"]
                obj["bone"]=socket["bone"]
                register(asset,obj,"SocketAttachment",bank,rows,socket,slot)
            entries.append(asset)
    return [body_id(sex),*entries]


def register(id,obj,tier,bank,rows,socket=None,source_slot=None):
    obj["layer"]=tier
    tri=len(obj.data.loop_triangles)
    bank[id]=obj
    row={"node":obj.name,"layer":tier,"triangles":tri,"bounds_m":bounds(obj.data),
         "materials":[m.name for m in obj.data.materials]}
    if socket:
        row["socket"]=socket
        row["source_slot"]=source_slot
    rows[id]=row


def to_staged_path(output,stage_directory):
    return stage_directory/output.name if stage_directory else output


def internal_files(directory):
    return {"blend":to_staged_path(BLEND,directory),"glb":to_staged_path(EXPORT,directory),
            "manifest":to_staged_path(MANIFEST,directory),"preview":directory or PREVIEWS}


def publish_sockets(rows,glb_path):
    index={n:Matrix(tuple(tuple(value[c*4+r] for c in range(4)) for r in range(4)))
           for n,_,value in skeleton(*read_glb(glb_path))}
    for objects in rows.values():
        for row in objects.values():
            socket=row.get("socket")
            if socket:
                m=socket["bind_matrix_gltf"]
                scene=Matrix(tuple(tuple(m[c*4+r] for c in range(4)) for r in range(4)))
                socket["joint_local_matrix_gltf"]=pack_matrix(index[socket["bone"]]@scene)


def preview_actor(scene,rig,bank,rows,row,tag,location,action=None,frame=0):
    actor=rig.copy()
    actor.name="Review_"+tag
    actor.animation_data_clear()
    scene.collection.objects.link(actor)
    actor.location=location
    for id in row["parts"]:
        prototype=bank[id]
        destination=prototype.copy()
        scene.collection.objects.link(destination)
        destination.name=f"{tag} / {id}"
        entry=rows[id]
        if entry["layer"]=="SocketAttachment":
            data=entry["socket"]
            bone=actor.data.bones[data["bone"]]
            destination.parent=actor
            destination.parent_type="BONE"
            destination.parent_bone=data["bone"]
            adjust=bone.matrix_local@Matrix.Translation((0,bone.length,0))
            destination.matrix_parent_inverse=adjust.inverted()
            flat=data["bind_matrix_blender"]
            destination.matrix_basis=Matrix(tuple(tuple(flat[c*4+r] for c in range(4)) for r in range(4)))
            continue
        destination.parent=actor
        destination.modifiers[0].object=actor
        if entry["layer"]=="Body":
            destination.data=prototype.data.copy()
            key=row["morph"]
            if key!="Normal":
                destination.data.shape_keys.key_blocks[key].value=1
            tint=row["skinTint"]
            if tint!="skin":
                factors=[a/b for a,b in zip(linear(PALETTE[tint]),linear(PALETTE["skin"]))]
                for value in destination.data.color_attributes["HumanTint"].data:
                    rgba=value.color
                    value.color=(*[min(1,c*f) for c,f in zip(rgba,factors)],rgba[3])
            if row["hide_regions"]:
                mask=destination.vertex_groups.new(name="VisibleSkin")
                allowed=[v.index for v,flag in zip(destination.data.vertices,
                         destination.data.attributes["_BODY_REGION"].data) if flag.value not in row["hide_regions"]
                         and not (row["head_covered"] and v.co.z>=1.571)
                         and not (row["gloves"] and v.co.z<.949)]
                mask.add(allowed,1,"REPLACE")
                modifier=destination.modifiers.new("Clothing visibility","MASK")
                modifier.vertex_group=mask.name
                modifier.use_smooth=True
    bpy.context.view_layer.update()
    if action:
        data=actor.animation_data_create()
        data.action=action
        if action.slots:
            data.action_slot=action.slots[0]
        bpy.context.view_layer.update()
        scene.frame_set(frame)
        posed=[b.matrix_basis.copy() for b in actor.pose.bones]
        assert max(b.to_quaternion().angle for b in posed)>.015,(tag,action.name,frame)
        actor.animation_data_clear()
        for b,matrix in zip(actor.pose.bones,posed):
            b.matrix_basis=matrix
    else:
        for side,sign in (("L",1),("R",-1)):
            bone=actor.pose.bones["upper_arm."+side]
            rotation=bone.matrix.to_quaternion()
            bone.rotation_mode="QUATERNION"
            bone.rotation_quaternion=rotation.inverted()@Quaternion((0,1,0),sign*.28)@rotation
    return actor


def review(name,items,rig,banks,rows,recipes,preview,render,*,turn=0,camera=None,scale=None,target=None):
    n=len(items)
    width=max(3.55,n*.95)
    spacing=.86 if turn else .91
    xs=[spacing*(i-(n-1)*.5) for i in range(n)]
    scene=studio(name,[(entry[0],x,-.24) for entry,x in zip(items,xs)],camera or (0,-9,2.55),scale or width)
    if target is not None:
        scene.camera.rotation_euler=(Vector(target)-scene.camera.location).to_track_quat("-Z","Y").to_euler()
    scene.render.resolution_x=1280
    scene.render.resolution_y=800 if n<=4 else 630
    if n>4:
        scene.render.resolution_x=1550
    for entry,x in zip(items,xs):
        label,tag,stage=entry[:3]
        action,frame=entry[3:] if len(entry)>3 else (None,0)
        view=preview_actor(scene,rig,banks[stage],rows[stage],recipes[tag][stage],label,(x,0,0),action,frame)
        view.rotation_euler.z=turn
    if render:
        scene.render.filepath=str(preview/(name+".png"))
        bpy.ops.render.render(write_still=True)
    return scene


def review_faces(rig,banks,rows,recipes,preview,render):
    name="HumanSheet_Face_Hair"
    entries=(("MALE / LOD0","Guard","LOD0",0),("FEMALE / LOD0","Explorer","LOD0",.35),
             ("FEMALE / LOD2","Explorer","LOD2",.35),("SIDE / LOD0","Resident","LOD0",1.33))
    positions=(-.405,-.135,.135,.405)
    scene=studio(name,[(entry[0],x,-.10) for entry,x in zip(entries,positions)],(0,-5,1.70),1.13)
    scene.camera.rotation_euler=(Vector((0,0,1.67))-scene.camera.location).to_track_quat("-Z","Y").to_euler()
    scene.render.resolution_x,scene.render.resolution_y=1280,435
    for obj in scene.objects:
        if obj.type=="FONT":
            obj.location.z=1.495
            obj.data.size=.023
    for (label,tag,stage,angle),x in zip(entries,positions):
        row=recipes[tag][stage]
        ids=[i for i in row["parts"] if rows[stage][i]["layer"] in {"Body","SkinAttachment"}]
        actor=preview_actor(scene,rig,banks[stage],rows[stage],{**row,"parts":ids},label,(x,0,0))
        actor.rotation_euler.z=angle
    if render:
        scene.render.filepath=str(preview/(name+".png"))
        bpy.ops.render.render(write_still=True)


def review_guard(rig,banks,rows,recipes,preview,render):
    name="HumanSheet_Guard_Armor"
    scene=studio(name,[],(0,-6,1.50),1.35)
    backdrop=next(o for o in scene.objects if o.type=="MESH")
    backdrop.hide_render=True
    backdrop.hide_set(True)
    scene.camera.rotation_euler=(Vector((0,0,1.468))-scene.camera.location).to_track_quat("-Z","Y").to_euler()
    scene.render.resolution_x,scene.render.resolution_y=1400,800
    for label,x,angle in (("Front",-.323,0),("Oblique",.323,-.67)):
        actor=preview_actor(scene,rig,banks["LOD0"],rows["LOD0"],recipes["Guard"]["LOD0"],label,(x,0,0))
        actor.rotation_euler.z=angle
    if render:
        scene.render.filepath=str(preview/(name+".png"))
        bpy.ops.render.render(write_still=True)
    return scene


def assembly_record(tag,stage,ids,parts):
    kit=kit_for(tag) if tag in ROLES else None
    profile=kit.profile if kit else tag
    _,build=profile.split("_")
    totals=(18000,8000,2500,2200) if tag in HEROES else (12000,5500,2000,1800)
    cost=sum(parts[i]["triangles"] for i in ids)
    budget=cost if stage=="Detail" else totals[STAGES.index(stage)]
    assert cost<=budget,(tag,stage,cost,budget)
    return {"parts":ids,"profile":profile,"morph":build,"triangles":cost,"cap":budget,
            "skinTint":kit.skin if kit else "skin","hide_regions":[1,2] if kit and stage!="ShadowProxy" else [],
            "head_covered":tag in SEALED,"gloves":tag in GLOVES}


def main():
    extras=sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else []
    out=Path(extras[extras.index("--output")+1]) if "--output" in extras else None
    destination=internal_files(out)
    render="--render" in extras
    for path in destination.values():
        (path if path==destination["preview"] else path.parent).mkdir(parents=True,exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    library=bpy.context.scene
    library.name="HumanSheet_LayerLibrary"
    library.unit_settings.system="METRIC"
    master=ROOT/"Source/Erma_Resource_Library.blend"
    sample_names=("Idle","Walk","Run","Aim_Base","Dodge_Left_L","Medical_Stabilized")
    with bpy.data.libraries.load(str(master),link=False) as (source,target):
        target.objects=["HUMAN_7H"]
        target.actions=["HUMAN_7H__"+name for name in sample_names]
    rig=target.objects[0]
    rig.animation_data_clear()
    library.collection.objects.link(rig)
    assert len(rig.data.bones)==23 and all(action is not None for action in target.actions)
    actions={a.name.removeprefix("HUMAN_7H__"):a for a in target.actions}
    for action in actions.values():
        action.use_fake_user=True
    for bone in rig.pose.bones:
        bone.matrix_basis.identity()

    paths=write_maps(TEXTURES)
    paths["Equipment"]=write_equipment_atlas(TEXTURES)
    mats=pbr_materials(paths)
    tints={key:linear(value) for key,value in PALETTE.items()}
    anchors=json.loads((ROOT/"Documentation/accessory_sockets.json").read_text(encoding="utf8"))["sockets"]
    banks={}
    rows={}
    recipes={tag:{} for tag in (*ROLES,*FIGURES)}
    all_groups=[]
    for stage in STAGES:
        groups=collections(library,stage)
        all_groups.extend(groups.values())
        bank=banks.setdefault(stage,{})
        row=rows.setdefault(stage,{})
        bodies={}
        for sex in ("Male","Female"):
            obj=shape_source(sex,stage,rig,groups["Body"],mats,tints)
            bodies[sex]=obj
            register(body_id(sex),obj,"Body",bank,row)
        for tag in ROLES:
            try:
                ids=attach_parts(tag,stage,choose_stage(stage,tag),rig,groups,mats,tints,anchors,bodies,bank,row)
                recipes[tag][stage]=assembly_record(tag,stage,ids,row)
            except Exception as error:
                raise RuntimeError(f"{stage}: {tag}") from error
        for tag in FIGURES:
            recipes[tag][stage]=assembly_record(tag,stage,[body_id(tag.split("_")[0])],row)
        print(f"{stage}: {len(bank)} assets, {max(r[stage]['triangles'] for r in recipes.values())} max assembly",
              flush=True)

    bpy.ops.object.select_all(action="DESELECT")
    for obj in library.objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active=rig
    bpy.ops.export_scene.gltf(filepath=str(destination["glb"]),export_format="GLB",use_active_scene=True,
                            use_selection=True,export_skins=True,export_def_bones=False,export_animations=False,
                            export_morph=True,export_morph_normal=True,export_extras=True,
                            export_attributes=True,export_texcoords=True,export_normals=True,
                            export_tangents=True,export_vertex_color="ACTIVE")
    publish_sockets(rows,destination["glb"])

    detail_scene=bpy.data.scenes.new("HumanSheet_Edit_Originals")
    detail_scene.unit_settings.system="METRIC"
    detail_scene.collection.objects.link(rig)
    bpy.context.window.scene=detail_scene
    detail_groups=collections(detail_scene,"Detail")
    detail_bank={}
    detail_rows={}
    detail_body={}
    detail_recipes={}
    for sex in ("Male","Female"):
        obj=shape_source(sex,"Detail",rig,detail_groups["Body"],mats,tints)
        register(body_id(sex),obj,"Body",detail_bank,detail_rows)
        detail_body[sex]=obj
    for tag in ROLES:
        ids=attach_parts(tag,"Detail",-1,rig,detail_groups,mats,tints,anchors,detail_body,detail_bank,detail_rows)
        detail_recipes[tag]={"Detail":assembly_record(tag,"Detail",ids,detail_rows)}
    for group in detail_groups.values():
        group.hide_viewport=True
        group.hide_render=True

    preview=destination["preview"]
    roles=[(name.replace("NPC_","").replace("_"," ").upper(),name,"LOD0") for name in ROLES[:5]]
    new=[(name.replace("NPC_","").replace("_"," ").upper(),name,"LOD0") for name in NEW_CHARACTERS]
    standard=[(name.replace("_"," ").upper(),name,"LOD0") for name in FIGURES]
    motion=[("IDLE","Player_Explorer","LOD0",actions["Idle"],6),
            ("WALK","Resident","LOD0",actions["Walk"],12),
            ("RUN","Ria_Engineer","LOD0",actions["Run"],8),
            ("AIM","Guard","LOD0",actions["Aim_Base"],0),
            ("DODGE","NPC_HeavyCombat","LOD0",actions["Dodge_Left_L"],6),
            ("SUPPORT","Medic","LOD0",actions["Medical_Stabilized"],10)]
    review("HumanSheet_Front",roles,rig,banks,rows,recipes,preview,render,turn=math.pi*.17)
    review("HumanSheet_New_Outfits",new,rig,banks,rows,recipes,preview,render,turn=math.pi*.17)
    review("HumanSheet_Bodies",standard,rig,banks,rows,recipes,preview,render,camera=(0,-9,1.28))
    review("HumanSheet_Quarter_Stances",motion[:3],rig,banks,rows,recipes,preview,render,turn=math.pi*.25)
    review("HumanSheet_Quarter_Actions",motion[3:5],rig,banks,rows,recipes,preview,render,turn=math.pi*.25)
    review("HumanSheet_Quarter_Support",motion[5:],rig,banks,rows,recipes,preview,render,turn=math.pi*.25,
           camera=(0,-6,2.1),scale=2.65,target=(0,0,.39))
    review("HumanSheet_LODs",[(stage,"Player_Explorer",stage) for stage in STAGES],
           rig,banks,rows,recipes,preview,render,turn=math.pi*.25)
    comparisons={**recipes,"Guard":{**recipes["Guard"],**detail_recipes["Guard"]}}
    review("HumanSheet_Original_vs_LOD0",[("DETAIL SOURCE","Guard","Detail"),("DELIVERY LOD0","Guard","LOD0")],
           rig,{**banks,"Detail":detail_bank},{**rows,"Detail":detail_rows},comparisons,preview,render,turn=math.pi*.25)
    review_faces(rig,banks,rows,recipes,preview,render)
    default=review_guard(rig,banks,rows,recipes,preview,render)
    for group in all_groups:
        group.hide_render=True
        group.hide_viewport=True
    bpy.context.window.scene=default
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=="VIEW_3D":
                area.spaces.active.region_3d.view_perspective="CAMERA"
                area.spaces.active.overlay.show_overlays=False
                area.spaces.active.shading.color_type="VERTEX"
                if screen==bpy.context.screen:
                    area.spaces.active.shading.type="MATERIAL"
    for scene in bpy.data.scenes:
        if scene.camera is not None:
            scene.render.filepath=f"//../Preview/HumanSheet/{scene.name}.png"
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(destination["blend"]),compress=True)

    shared={}
    for ref,(tag,slot,_) in SHARED_ACCESSORIES.items():
        found=[i for i in recipes[tag]["LOD0"]["parts"] if rows["LOD0"][i].get("source_slot")==slot]
        assert len(found)==1,(ref,tag,slot,found)
        shared[ref]=found[0]
    sheets=["Man_Default.png","Women_Default.png","Shared_Character_Parts.png",*(kit.sheet for kit in KITS.values())]
    manifest={"schema":"erma.human_layers.v4","date":"2026-11-24",
              "source":BLEND.relative_to(ROOT).as_posix(),"part_bank":EXPORT.relative_to(ROOT).as_posix(),
              "source_sha256":digest(destination["blend"]),"part_bank_sha256":digest(destination["glb"]),
              "rig":"HUMAN_7H","bone_count":23,"scale_m":1.8,
              "reference_images":{name:digest(ROOT/"Reference/Characters"/name) for name in sheets},
              "anatomy_reference":{"panels":["left male","middle female"],
                  "sha256":"86dcdc7008277b160233c4e5b849a65f8aab54175df7038784df31989b0d4da9"},
              "body_region_attribute":"_BODY_REGION","body_regions":{"0":"head, neck, hands","1":"upper body",
                  "2":"pelvis and legs"},"body_morphs":["Lean","Heavy"],"normal_body_morph":"all zero",
              "texture_max_px":1024,"textures":{key:{kind:{"file":file.relative_to(ROOT).as_posix(),
                                                            "sha256":digest(file)}
                                                      for kind,file in items.items()} for key,items in paths.items()},
              "socket_axis":"Blender part +X forward, +Z up. glTF part +X forward, +Y up.",
              "animation_banks":["Export/Characters/Player_Explorer.glb",
                                 *[p.relative_to(ROOT).as_posix() for p in
                                   sorted((ROOT/"Export/AnimationBanks").glob("HUMAN_7H_*.glb"))]],
              "shared_accessory_sheet_parts":shared,"parts":rows,
              "equipment_surface_atlas":{"material":"HumanSheet_Equipment","resolution":[1024,1024],
                  "usage":"UV paint, insignia, recessed badge and tool marks on the existing armor surface."},
              "detail_source_triangles":{key:row["triangles"] for key,row in detail_rows.items()},
              "assemblies":recipes,"integration":"offline R4 character delivery. Game runtime connection pending."}
    destination["manifest"].write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print("HUMAN_SHEET_LAYERED_DONE",len(recipes),flush=True)


if __name__=="__main__":
    main()
