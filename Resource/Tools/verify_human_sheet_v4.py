"""Check the character bank, texture layout, anatomical joins and Blender reimport."""
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_human_reference_v4 import STAGES, part_cap, skeleton, verify_part
from verify_resource_pack import ROOT, accessor, read_glb


def png_size(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n" and data[12:16] == b"IHDR"
    return struct.unpack_from(">II", data, 16)


def buffer_view(doc, binary, index):
    view = doc["bufferViews"][index]
    assert view["buffer"] == 0
    start = view.get("byteOffset", 0)
    return binary[start:start + view["byteLength"]]


def section(doc,binary,nodes,height):
    points=[]
    for node in nodes:
        for primitive in doc["meshes"][node["mesh"]]["primitives"]:
            positions=accessor(doc,binary,primitive["attributes"]["POSITION"])
            indices=[i for (i,) in accessor(doc,binary,primitive["indices"])]
            for a,b,c in zip(indices[::3],indices[1::3],indices[2::3]):
                for first,last in ((positions[a],positions[b]),(positions[b],positions[c]),(positions[c],positions[a])):
                    if first[1]==height:
                        points.append((first[0],first[2]))
                    if (first[1]-height)*(last[1]-height)<0:
                        t=(height-first[1])/(last[1]-first[1])
                        points.append(tuple(first[i]+t*(last[i]-first[i]) for i in (0,2)))
    assert points,height
    xs,ys=zip(*points)
    return min(xs),max(xs),max(ys)-min(ys)


def verify_proportions(doc,binary,manifest,nodes):
    measured={}
    for sex in ("Male","Female"):
        for build in ("Heavy","Normal","Lean"):
            tag=f"{sex}_{build}"
            parts=manifest["assemblies"][tag]["LOD0"]["parts"]
            bank=manifest["parts"]["LOD0"]
            def cut(z,*suffixes):
                group=[nodes[bank[p]["node"]] for p in parts if p.endswith(suffixes) or
                       any(p.endswith(suffix+"_skin") for suffix in suffixes)]
                return section(doc,binary,group,z)
            l,r,_=cut(1.403,"_Torso","_Arm_L","_Arm_R")
            left,right,depth=cut(1.155,"_Torso")
            al,ar,ad=cut(1.062,"_Arm_L")
            tl,tr,td=cut(.819,"_Leg_L")
            (_,tright,_)=cut(.819,"_Leg_R")
            (kl,_,_)=cut(.55,"_Leg_L")
            (_,kr,_)=cut(.55,"_Leg_R")
            fl,_,_=cut(.040,"_Foot_L")
            _,fr,_=cut(.040,"_Foot_R")
            measured[tag]={"shoulder_width":r-l,"waist_width":right-left,"waist_depth":depth,
                           "forearm_width":ar-al,"forearm_depth":ad,"thigh_width":tr-tl,
                           "thigh_depth":td,"thigh_gap":tl-tright,
                           "knee_gap":kl-kr,"foot_gap":fl-fr}
        widths=[measured[f"{sex}_{build}"] for build in ("Heavy","Normal","Lean")]
        for key in widths[0]:
            a,b,c=(profile[key] for profile in widths)
            assert a>b>c>0,(sex,key,a,b,c)
    return {tag:{metric:round(value*1000,2) for metric,value in values.items()} for tag,values in measured.items()}


def verify_export(manifest):
    names = manifest["reference_images"]
    assert len(names) == 13 and "Shared_Character_Parts.png" in names
    for name, sha in names.items():
        assert hashlib.sha256((ROOT / "Reference/Characters" / name).read_bytes()).hexdigest() == sha
    files = (("source", "source_sha256"), ("part_bank", "part_bank_sha256"))
    for file, sha in files:
        assert hashlib.sha256((ROOT / manifest[file]).read_bytes()).hexdigest() == manifest[sha]

    size_limit = manifest["texture_max_px"]
    assert size_limit == 1024
    maps = {}
    for surface, channels in manifest["textures"].items():
        assert set(channels) == ({"BaseColor"} if surface=="HairCards" else {"BaseColor", "Normal", "ORM"})
        maps[surface] = {}
        for name, info in channels.items():
            data = (ROOT / info["file"]).read_bytes()
            assert hashlib.sha256(data).hexdigest() == info["sha256"]
            size = png_size(data)
            assert min(size) > 0 and max(size) <= size_limit, (name, size)
            maps[surface][name] = size
    assert set(maps) == {"Fabric", "Shell", "Skin", "HairCards"}

    doc, binary = read_glb(ROOT / manifest["part_bank"])
    assert len(doc["skins"]) == 1 and not doc.get("animations") and not doc.get("extensionsRequired")
    actual = skeleton(doc, binary)
    expected = skeleton(*read_glb(ROOT / "Export/Characters/Player_Explorer.glb"))
    assert len(actual) == len(expected) == manifest["bone_count"] == 23
    assert [r[:2] for r in actual] == [r[:2] for r in expected]
    bind_delta = max(abs(x-y) for (_, _, a), (_, _, b) in zip(actual, expected) for x, y in zip(a, b))
    assert bind_delta < 2e-5

    images = doc["images"]
    assert len(images) == 10
    image_sizes = [png_size(buffer_view(doc, binary, image["bufferView"])) for image in images]
    assert all(max(size) <= size_limit for size in image_sizes)
    materials = {row["name"]: row for row in doc["materials"]}
    assert set(materials) == {"HumanSheet_" + name for name in (*maps, "Glass")}
    assert materials["HumanSheet_Glass"]["alphaMode"] == "BLEND"
    hair_material=materials["HumanSheet_HairCards"]
    assert hair_material["alphaMode"]=="MASK" and hair_material.get("alphaCutoff",.5)==.5
    image_refs = set()
    for surface, channels in maps.items():
        material = materials["HumanSheet_" + surface]
        pbr = material["pbrMetallicRoughness"]
        ids = {"BaseColor": pbr["baseColorTexture"]["index"]}
        if surface!="HairCards":
            orm = pbr["metallicRoughnessTexture"]["index"]
            assert material["occlusionTexture"]["index"] == orm
            ids.update(Normal=material["normalTexture"]["index"],ORM=orm)
        else:
            source=doc["textures"][ids["BaseColor"]]["source"]
            assert buffer_view(doc,binary,images[source]["bufferView"])[25]==6
        for name, index in ids.items():
            image = doc["textures"][index]["source"]
            assert image_sizes[image] == channels[name]
            image_refs.add(image)
    assert len(image_refs) == len(images)

    nodes = {n["name"]: n for n in doc["nodes"] if "mesh" in n}
    stages = manifest["parts"]
    assert tuple(stages) == STAGES and len(nodes) == sum(len(bank) for bank in stages.values())
    assert set(stages["LOD0"]) == set(manifest["detail_source_triangles"])
    stats = {}
    for stage, bank in stages.items():
        cards=0
        for id, row in bank.items():
            node = nodes[row["node"]]
            extras = node["extras"]
            assert extras["asset_id"] == id and extras["lod"] == stage
            verify_part(doc,binary,node,row,part_cap(row,stage),len(actual),None)
            if stage == "ShadowProxy":
                assert row["materials"] == ["HumanSheet_Skin"]
            if "HumanSheet_HairCards" in row["materials"]:
                assert stage=="LOD0" and "_Hair_" in id
                cards+=1
        totals = []
        for recipe in manifest["assemblies"].values():
            assembly = recipe[stage]
            ids = assembly["parts"]
            assert len(set(ids)) == len(ids)
            triangles = sum(bank[id]["triangles"] for id in ids)
            assert 0 < triangles == assembly["triangles"] <= assembly["cap"]
            totals.append(triangles)
        stats[stage] = {"part_count": len(bank),
                        "largest_part_triangles": max(row["triangles"] for row in bank.values()),
                        "hair_card_parts":cards,
                        "assembly_triangles": [min(totals), max(totals)]}
    assert stats["LOD0"]["hair_card_parts"] == 6
    assert len(manifest["assemblies"]) == 17
    accessories=manifest["shared_accessory_sheet_parts"]
    assert len(accessories)==len(set(accessories.values()))==9
    assert set(accessories.values())<=stages["LOD0"].keys()
    regular=manifest["assemblies"]["Guard"]["LOD0"]["parts"]
    helmet=manifest["assemblies"]["NPC_Guard_Helmet"]["LOD0"]["parts"]
    assert set(helmet)=={p for p in regular if "_Shared_Hair_" not in p and "_Shared_Face_" not in p} | {
        accessories["Helmet_Guard"]}
    proportions=verify_proportions(doc,binary,manifest,nodes)
    return {"reference_sheets":len(names),"assemblies":len(manifest["assemblies"]),"stages":stats,
            "rig_bones":len(actual),"inverse_bind_max_delta":bind_delta,"texture_count":len(images),
            "texture_sizes_px":maps,"body_cross_sections_mm":proportions,
            "shared_accessories_verified":len(accessories)}


def body_edges(parts):
    edges=defaultdict(list)
    for obj in parts:
        local=defaultdict(list)
        for face in obj.data.polygons:
            ids=list(face.vertices)
            for a,b in zip(ids,ids[1:]+ids[:1]):
                local[tuple(sorted((a,b)))].append((a,b))
        for directed in local.values():
            if len(directed)==1:
                a,b=directed[0]
                pos_a,pos_b=(tuple(round(d,7) for d in obj.data.vertices[i].co) for i in (a,b))
                key=tuple(sorted((pos_a,pos_b)))
                edges[key].append((obj,a,b) if pos_a<pos_b else (obj,b,a))
    return edges


def body_ids(recipe):
    suffixes=("_Torso","_Neck","_Arm_R","_Arm_L","_Leg_R","_Leg_L")
    return [id for id in recipe["parts"] if id.endswith(suffixes) or "_Shared_Head_" in id
            or "_Shared_Neck_" in id]


def skin_weights(obj,i):
    return sorted((obj.vertex_groups[g.group].name,round(g.weight,6))
                  for g in obj.data.vertices[i].groups if g.weight>1e-7)


def verify_joins(bpy,manifest):
    all_edges=0
    for key,assembly in manifest["assemblies"].items():
        for stage,recipe in assembly.items():
            if stage=="ShadowProxy":
                continue
            objs=[bpy.data.objects[manifest["parts"][stage][id]["node"]] for id in body_ids(recipe)]
            assert len(objs)==7,(key,stage,[o.name for o in objs])
            for point,parts in body_edges(objs).items():
                assert len(parts)==2,(key,stage,point,[p[0].name for p in parts])
                (a,i,j),(b,k,m)=parts
                assert a!=b
                assert skin_weights(a,i)==skin_weights(b,k) and skin_weights(a,j)==skin_weights(b,m)
                all_edges+=1
    return all_edges


def verify_motion(bpy,manifest):
    from build_human_sheet_v4 import clone_actor

    samples=(("Explorer","Aim_Walk_Back",9),("Explorer","Aim_Walk_Left",15),("Guard","Sprint",8),
             ("Medic","GetUp_Front",21),("Pilot","Ride_Base",0),("Resident","Walk",8),
             ("Player_Explorer","Aim_Run_Forward",8),("Ria_Engineer","Run",8),("NPC_Researcher","Flee_Run",8),
             ("NPC_Miner","Jump_Full",15),("NPC_HeavyCombat","Dodge_Left_L",5),("NPC_Guard_Helmet","Hit_Front",4))
    scene=bpy.data.scenes.new("Verification")
    bpy.context.window.scene=scene
    rig=bpy.data.scenes["HumanSheet_Part_Library"].objects["HUMAN_7H"]
    bank={id:bpy.data.objects[row["node"]] for id,row in manifest["parts"]["LOD0"].items()}
    names=["HUMAN_7H__"+motion for _,motion,_ in samples]
    with bpy.data.libraries.load(str(ROOT/"Source/Erma_Resource_Library.blend"),link=False) as (source,target):
        assert set(names)<=set(source.actions)
        target.actions=names
    results={}
    for (tag,name,frame),action in zip(samples,target.actions):
        recipe=manifest["assemblies"][tag]["LOD0"]
        actor=clone_actor(scene,bank,rig,recipe["parts"],tag,(0,0,0))
        for bone in actor.pose.bones:
            bone.matrix_basis.identity()
        animator=actor.animation_data_create()
        animator.action=action
        if action.slots:
            animator.action_slot=action.slots[0]
        scene.frame_set(frame)
        graph=bpy.context.evaluated_depsgraph_get()
        ids=set(body_ids(recipe))
        objs=[o for o in actor.children if o["asset_id"] in ids]
        meshes={o:o.evaluated_get(graph).data for o in objs}
        gap=0.
        for (a,i,j),(b,k,m) in body_edges(objs).values():
            for ai,bi in ((i,k),(j,m)):
                gap=max(gap,(meshes[a].vertices[ai].co-meshes[b].vertices[bi].co).length)
        assert gap<.00002,(name,gap)
        results[name]={"frame":frame,"max_body_seam_gap_m":round(gap,8)}
    return results


def verify_blender(manifest):
    import bpy

    bpy.ops.wm.open_mainfile(filepath=str(ROOT / manifest["source"]))
    source=bpy.data.scenes["HumanSheet_Detail_Source"]
    detailed={obj["asset_id"]:obj for obj in source.objects if obj.type=="MESH"}
    assert len(detailed) == len(manifest["detail_source_triangles"])
    for id, count in manifest["detail_source_triangles"].items():
        mesh=detailed[id].data
        mesh.calc_loop_triangles()
        assert len(mesh.loop_triangles) == count, id
    packed=[img for img in bpy.data.images if img.name.startswith("HumanSheet_")]
    assert len(packed) == 10 and all(img.packed_file and max(img.size) <= 1024 for img in packed)
    assert bpy.context.scene.name == "HumanSheet_Front"
    joins=verify_joins(bpy,manifest)
    motion=verify_motion(bpy,manifest)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    assert bpy.ops.import_scene.gltf(filepath=str(ROOT / manifest["part_bank"])) == {"FINISHED"}
    objects=bpy.context.scene.objects
    armatures=[obj for obj in objects if obj.type=="ARMATURE"]
    assert len(armatures)==1 and len(armatures[0].data.bones)==23
    rig=armatures[0]
    meshes=[obj for obj in objects if obj.type=="MESH" and "asset_id" in obj]
    expected={row["node"] for bank in manifest["parts"].values() for row in bank.values()}
    assert {obj.name for obj in meshes} == expected and len(meshes)==len(expected)
    assert all(any(mod.type=="ARMATURE" and mod.object==rig for mod in obj.modifiers) for obj in meshes)
    for bone_name in ("upper_arm.R", "forearm.R", "thigh.L", "shin.L"):
        bone=rig.pose.bones[bone_name]
        bone.rotation_mode="XYZ"
        bone.rotation_euler.x=.40
    bpy.context.view_layer.update()
    graph=bpy.context.evaluated_depsgraph_get()
    deformation={}
    for key in ("Arm_R", "Leg_L", "WristScanner_R", "LegPanels_L"):
        obj=objects["LOD0__Sheet_Explorer_"+key]
        evaluated=obj.evaluated_get(graph)
        movement=max((a.co-b.co).length for a,b in zip(obj.data.vertices,evaluated.data.vertices))
        assert math.isfinite(movement) and .01 < movement < 1, key
        deformation[key]=round(movement,6)
    return {"detail_source_parts":len(detailed),"packed_textures":len(packed),
            "reimported_parts":len(meshes),"sample_vertex_displacement_m":deformation,
            "paired_boundary_edges":joins,"source_motion_poses":motion}


if __name__=="__main__":
    manifest=json.loads((ROOT / "Documentation/human_sheet_v4.json").read_text(encoding="utf-8"))
    if manifest["schema"]=="erma.human_layers.v4":
        from verify_human_sheet_layers import main
        main()
        sys.exit(0)
    report=verify_export(manifest)
    if "--with-blender" in sys.argv:
        report["blender"]=verify_blender(manifest)
        (ROOT / "Validation/human_sheet_v4_report.json").write_text(
            json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2))
