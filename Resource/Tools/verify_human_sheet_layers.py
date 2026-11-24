"""Validate the shared bodies, wardrobe, static sockets and assembly budgets."""
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_human_reference_v4 import part_cap, skeleton
from verify_human_sheet_v4 import buffer_view, png_size
from verify_resource_pack import ROOT, accessor, read_glb


STAGES=("LOD0","LOD1","LOD2","ShadowProxy")


def hash_of(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def multiply(a,b):
    return [sum(a[k*4+r]*b[c*4+k] for k in range(4)) for c in range(4) for r in range(4)]


def check_geometry(doc,binary,node,row):
    primitives=doc["meshes"][node["mesh"]]["primitives"]
    skinned=row["layer"]!="SocketAttachment"
    assert ("skin" in node)==skinned and (not skinned or node["skin"]==0)
    needed={"POSITION","NORMAL","TEXCOORD_0","TANGENT","COLOR_0"}
    if skinned:
        needed|={"JOINTS_0","WEIGHTS_0"}
    if row["layer"]=="Body":
        needed.add("_BODY_REGION")
        assert doc["meshes"][node["mesh"]]["extras"]["targetNames"]==["Lean","Heavy"]
    count=0
    region_ids=set()
    materials=set()
    used_bones=set()
    for prim in primitives:
        attrs=prim["attributes"]
        assert needed<=attrs.keys() and prim.get("mode",4)==4,(node["name"],needed-attrs.keys())
        data={key:accessor(doc,binary,index) for key,index in attrs.items()}
        p=data["POSITION"]
        normals=data["NORMAL"]
        tangents=data["TANGENT"]
        assert all(math.isfinite(x) for values in data.values() for v in values for x in v),node["name"]
        assert all(len(v)==len(p) for v in data.values())
        for normal,tangent in zip(normals,tangents):
            assert abs(sum(n*n for n in normal)-1)<.005,node["name"]
            assert abs(sum(t*t for t in tangent[:3])-1)<.005,node["name"]
            assert abs(sum(n*t for n,t in zip(normal,tangent)))<.005,node["name"]
            assert abs(abs(tangent[3])-1)<.003,node["name"]
        indices=[i for (i,) in accessor(doc,binary,prim["indices"])]
        assert len(indices)%3==0 and max(indices)<len(p)
        for a,b,c in zip(indices[0::3],indices[1::3],indices[2::3]):
            u=[p[b][i]-p[a][i] for i in range(3)]
            v=[p[c][i]-p[a][i] for i in range(3)]
            assert sum((u[i]*v[i-1]-v[i]*u[i-1])**2 for i in range(3))>1e-23,(node["name"],a,b,c)
        count+=len(indices)//3
        material_name=doc["materials"][prim["material"]]["name"]
        materials.add(material_name)
        if material_name=="HumanSheet_Equipment":
            uv=data["TEXCOORD_0"]
            assert all(0<u<1 and 0<v<1 for u,v in uv),node["name"]
            for a,b,c in zip(indices[0::3],indices[1::3],indices[2::3]):
                u,v=uv[a]
                x,y=uv[b]
                w,z=uv[c]
                assert abs((x-u)*(z-v)-(w-u)*(y-v))>1e-9,node["name"]
        if skinned:
            weights=doc["accessors"][attrs["WEIGHTS_0"]]
            scale={5121:255,5123:65535}.get(weights["componentType"],1) if weights.get("normalized") else 1
            for joints,values in zip(data["JOINTS_0"],data["WEIGHTS_0"]):
                assert len(joints)==4 and max(joints)<23 and abs(sum(values)/scale-1)<.003
                assert all(value==0 or value/scale>=.008 for value in values),node["name"]
                used_bones.update(joint for joint,value in zip(joints,values) if value>0)
        else:
            assert not any(a.startswith(("JOINTS","WEIGHTS")) for a in attrs)
        if row["layer"]=="Body":
            region_ids.update(value[0] for value in data["_BODY_REGION"])
            assert len(prim["targets"])==2
            assert all(doc["accessors"][t["POSITION"]]["count"]==len(p) for t in prim["targets"])
    assert count==row["triangles"] and materials==set(row["materials"]),node["name"]
    if row["layer"]=="Body":
        assert region_ids=={0,1,2}
    if row["layer"]=="Clothing":
        assert node["extras"]["weight_source"].endswith("Nearest Face Interpolated")
        assert len(used_bones)>=(3 if node["name"].endswith("_Coat") else 13),(node["name"],used_bones)


def validate_export(manifest,files):
    for key,field in (("source","source_sha256"),("part_bank","part_bank_sha256")):
        assert hash_of(files[key])==manifest[field]
    assert len(manifest["reference_images"])==13
    for filename,sha in manifest["reference_images"].items():
        assert hash_of(ROOT/"Reference/Characters"/filename)==sha,filename
    assert manifest["texture_max_px"]==1024
    for maps in manifest["textures"].values():
        for row in maps.values():
            source=ROOT/row["file"]
            assert hash_of(source)==row["sha256"] and max(png_size(source.read_bytes()))<=1024
    for bank in manifest["animation_banks"]:
        assert read_glb(ROOT/bank)[0].get("animations")
    doc,binary=read_glb(files["part_bank"])
    map_count=sum(len(group) for group in manifest["textures"].values())
    assert len(doc["images"])==map_count and len(doc["skins"])==1 and not doc.get("animations")
    assert all(max(png_size(buffer_view(doc,binary,n["bufferView"])))<=1024 for n in doc["images"])
    assert len(doc.get("extensionsRequired",[]))==0
    materials={row["name"]:row for row in doc["materials"]}
    assert materials["HumanSheet_HairCards"]["alphaMode"]=="MASK" and materials["HumanSheet_Glass"]["alphaMode"]=="BLEND"
    atlas=manifest.get("equipment_surface_atlas")
    if atlas:
        material=materials[atlas["material"]]
        pbr=material["pbrMetallicRoughness"]
        links=(pbr["baseColorTexture"],material["normalTexture"],pbr["metallicRoughnessTexture"])
        images={doc["textures"][link["index"]]["source"] for link in links}
        assert len(images)==3 and all(png_size(buffer_view(doc,binary,doc["images"][i]["bufferView"]))==(1024,1024)
                                      for i in images)
        for stage in ("LOD0","LOD1","LOD2"):
            parts=manifest["parts"][stage]
            for suffix in ("ChestShell_chest","ChestShell_spine","ShoulderCap_L","ShoulderCap_R"):
                assert atlas["material"] in parts["Human_Guard_"+suffix]["materials"]
    old=skeleton(*read_glb(ROOT/"Export/Characters/Player_Explorer.glb"))
    rig=skeleton(doc,binary)
    assert [row[:2] for row in old]==[row[:2] for row in rig] and len(rig)==23
    bind=max(abs(x-y) for (_,_,a),(_,_,b) in zip(rig,old) for x,y in zip(a,b))
    assert bind<1e-7
    inverse={n:values for n,_,values in rig}
    nodes={node["name"]:node for node in doc["nodes"] if "mesh" in node}
    skins={}
    counts={}
    for stage,parts in manifest["parts"].items():
        totals=Counter(row["layer"] for row in parts.values())
        counts[stage]=dict(totals)
        assert totals["Body"]==2
        for id,row in parts.items():
            node=nodes[row["node"]]
            assert node["extras"]["asset_id"]==id and node["extras"]["lod"]==stage
            check_geometry(doc,binary,node,row)
            socket=row.get("socket")
            if socket:
                assert row["triangles"]<=part_cap(row,stage),(stage,id,row["triangles"])
                assert node.get("translation",[0,0,0])==[0,0,0]
                assert node.get("rotation",[0,0,0,1])==[0,0,0,1]
                flat=multiply(inverse[socket["bone"]],socket["bind_matrix_gltf"])
                assert max(abs(a-b) for a,b in zip(flat,socket["joint_local_matrix_gltf"]))<2e-6
            elif row["layer"]=="Body":
                skins[(stage,id)]=node["skin"]
    assert set(skins.values())=={0}
    assert len(nodes)==sum(sum(stage.values()) for stage in counts.values())
    assert len(manifest["assemblies"])==17 and len(manifest["shared_accessory_sheet_parts"])==9
    assembly_max={}
    for stage in STAGES:
        high=0
        for tag,whole in manifest["assemblies"].items():
            a=whole[stage]
            parts=[manifest["parts"][stage][i] for i in a["parts"]]
            assert sum(p["triangles"] for p in parts)==a["triangles"]<=a["cap"]
            assert sum(p["layer"]=="Body" for p in parts)==1
            assert sum(p["layer"]=="Clothing" for p in parts)<=2
            assert a["morph"] in ("Lean","Normal","Heavy")
            high=max(high,a["triangles"])
        assembly_max[stage]=high
    return {"skinning_rig_bones":23,"max_inverse_bind_delta_m":bind,"body_masters_per_stage":2,
            "assemblies":len(manifest["assemblies"]),"part_layers":counts,
            "assembled_max_triangles":assembly_max,"embedded_maps":len(doc["images"]),
            "shared_accessories":len(manifest["shared_accessory_sheet_parts"])}


def largest_component(mesh):
    near=defaultdict(list)
    for edge in mesh.edges:
        a,b=edge.vertices
        near[a].append(b)
        near[b].append(a)
    remaining=set(range(len(mesh.vertices)))
    biggest=set()
    while remaining:
        vertex=remaining.pop()
        stack=[vertex]
        component={vertex}
        while stack:
            for index in near[stack.pop()]:
                if index in remaining:
                    remaining.remove(index)
                    component.add(index)
                    stack.append(index)
        if len(component)>len(biggest):
            biggest=component
    return biggest


def body_measurements(obj):
    mesh=obj.data
    mesh.calc_loop_triangles()
    result={}
    for name in ("Heavy","Basis","Lean"):
        coords=[item.co for item in mesh.shape_keys.key_blocks[name].data]
        def slice_x(z):
            xs=[]
            for triangle in mesh.loop_triangles:
                a,b,c=(coords[i] for i in triangle.vertices)
                for p,q in ((a,b),(b,c),(c,a)):
                    if (p.z-z)*(q.z-z)<0:
                        xs.append(p.x+(q.x-p.x)*(z-p.z)/(q.z-p.z))
            return xs
        shoulder=slice_x(1.402)
        knees=slice_x(.55)
        feet=slice_x(.039)
        assert shoulder and knees and feet
        result[name]={"shoulders":(max(shoulder)-min(shoulder))*1000,
                      "knee_gap":2*min(abs(x) for x in knees)*1000,
                      "foot_gap":2*min(abs(x) for x in feet)*1000}
    for key in result["Heavy"]:
        assert result["Heavy"][key]>result["Basis"][key]>result["Lean"][key]>0,(obj.name,key)
    return {name:{metric:round(value,2) for metric,value in data.items()} for name,data in result.items()}


def validate_blender(manifest,files):
    import bpy

    bpy.ops.wm.open_mainfile(filepath=str(files["source"]))
    anatomy={}
    morphology={}
    for id,row in manifest["parts"]["LOD0"].items():
        if row["layer"]!="Body":
            continue
        obj=bpy.data.objects[row["node"]]
        surface=largest_component(obj.data)
        p=[obj.data.vertices[i].co for i in surface]
        assert min(co.z for co in p)<.01 and max(co.z for co in p)>1.78
        assert min(co.x for co in p)<-.44 and max(co.x for co in p)>.44
        assert len(surface)>len(obj.data.vertices)*.75
        anatomy[id]=len(surface)
        morphology[id]=body_measurements(obj)
    if manifest.get("equipment_surface_atlas"):
        id="Human_Guard_ChestShell_chest"
        node=manifest["parts"]["LOD0"][id]["node"]
        chest=bpy.data.objects[node].data
        assert len(largest_component(chest))==len(chest.vertices)
        assert any(p.use_smooth for p in chest.polygons) and any(not p.use_smooth for p in chest.polygons)
        edges=Counter(tuple(sorted(e)) for polygon in chest.polygons for e in polygon.edge_keys)
        assert all(count==2 for count in edges.values()),id
    checked={}
    for name in ("HumanSheet_Quarter_Stances","HumanSheet_Quarter_Actions","HumanSheet_Quarter_Support"):
        scene=bpy.data.scenes[name]
        bpy.context.window.scene=scene
        bpy.context.view_layer.update()
        graph=bpy.context.evaluated_depsgraph_get()
        for actor in [o for o in scene.objects if o.type=="ARMATURE"]:
            angle=max(b.matrix_basis.to_quaternion().angle for b in actor.pose.bones)
            assert angle>.015,actor.name
            for obj in actor.children:
                if obj.type=="MESH":
                    assert all(math.isfinite(c) for v in obj.evaluated_get(graph).data.vertices for c in v.co)
                    if obj.get("layer")=="SocketAttachment":
                        assert obj.parent_type=="BONE" and not obj.modifiers
            checked[actor.name.removeprefix("Review_")]=round(math.degrees(angle),2)
    assert len(checked)==6
    bpy.ops.wm.read_factory_settings(use_empty=True)
    assert bpy.ops.import_scene.gltf(filepath=str(files["part_bank"]))=={"FINISHED"}
    nodes=[o for o in bpy.context.scene.objects if o.type=="MESH" and "asset_id" in o]
    assert len(nodes)==sum(len(v) for v in manifest["parts"].values())
    rig=next(o for o in bpy.context.scene.objects if o.type=="ARMATURE")
    assert len(rig.data.bones)==23
    for obj in nodes:
        if obj["layer"]=="SocketAttachment":
            assert not any(m.type=="ARMATURE" for m in obj.modifiers)
        else:
            assert any(m.type=="ARMATURE" and m.object==rig for m in obj.modifiers)
    return {"connected_body_vertices":anatomy,"body_cross_sections_mm":morphology,
            "reimported_meshes":len(nodes),"animated_pose_delta_degrees":checked}


def main():
    args=sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else sys.argv[1:]
    stage=Path(args[args.index("--output")+1]) if "--output" in args else None
    path=(stage if stage else ROOT/"Documentation")/"human_sheet_v4.json"
    manifest=json.loads(path.read_text(encoding="utf8"))
    assert manifest["schema"]=="erma.human_layers.v4"
    files={k:(stage/Path(manifest[k]).name if stage else ROOT/manifest[k])
           for k in ("source","part_bank")}
    report=validate_export(manifest,files)
    if "--with-blender" in args:
        report["blender"]=validate_blender(manifest,files)
        (stage if stage else ROOT/"Validation").joinpath("human_sheet_v4_report.json").write_text(
            json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(json.dumps(report,indent=2))


if __name__=="__main__":
    main()
