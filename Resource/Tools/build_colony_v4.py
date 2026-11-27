"""Export the v4 colony building and prop library, including four authored tiers."""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
from build_cq04b_city_kit import objects
from colony_building_forms import NEW, REPLACEMENTS, building, measured_specs
from colony_prop_forms import GROUPS, prop
from verify_resource_pack import read_glb

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"Export/ColonyV4"
TEX=ROOT/"Export/Textures/ColonyV4"
TIERS=("LOD0","LOD1","LOD2","ShadowProxy")
PALETTE={
    "clad":((.94,.89,.79),.47,.37,"Colony_WeatheredCladding",0),
    "armor":((.32,.49,.55),.52,.57,"Colony_WeatheredCladding",0),
    "blue_paint":((.29,.65,.82),.52,.57,"Colony_WeatheredCladding",0),
    "oxide_paint":((1,.56,.25),.52,.57,"Colony_WeatheredCladding",0),
    "dark":((.173,.229,.25),.69,.37,"Interior_Bulkhead",0),
    "grating":((.77,.81,.83),.52,.64,"Industrial_GratingTrim",0),
    "bulkhead":((.89,.84,.73),.69,.23,"Interior_Bulkhead",0),
    "glass":((.61,.83,.99),.24,.63,"Colony_GlassCurtain",.18),
    "sign":((.61,.89,.92),.32,.38,"Storefront_SignageAtlas",.72),
    "cyan":((.17,.72,.76),.32,.26,None,.6),
    "amber":((.81,.42,.14),.44,.28,None,.30),
    "bark":((.43,.64,.49),.76,0,"Alien_FoliageBark",0),
    "foliage":((.38,.81,.55),.76,0,None,0),
}


def make_materials():
    mats={}
    for key,(color,rough,metal,_,emission) in PALETTE.items():
        mat=bpy.data.materials.new("Colony_"+key)
        mat.diffuse_color=(*color,1)
        mat.use_nodes=True
        p=mat.node_tree.nodes["Principled BSDF"]
        p.inputs["Base Color"].default_value=(*color,1)
        p.inputs["Roughness"].default_value=rough
        p.inputs["Metallic"].default_value=metal
        if emission:
            p.inputs["Emission Color"].default_value=(*color,1)
            p.inputs["Emission Strength"].default_value=emission
        mats[key]=mat
    return mats


def images_for_blender(mats):
    for key,mat in mats.items():
        color,rough,metal,family,strength=PALETTE[key]
        if not family:
            continue
        tree=mat.node_tree
        principled=tree.nodes["Principled BSDF"]
        for prefix in ("BaseColor","Normal","ORM","Emissive"):
            image=bpy.data.images.get(family+"_"+prefix+".png")
            if image is None:
                image=bpy.data.images.load(str(TEX/(family+"_"+prefix+".png")))
                image.colorspace_settings.name="Non-Color" if prefix in {"ORM","Normal"} else "sRGB"
            sampler=tree.nodes.new("ShaderNodeTexImage")
            sampler.image=image
            if prefix=="Normal":
                n=tree.nodes.new("ShaderNodeNormalMap")
                n.inputs["Strength"].default_value=.54
                tree.links.new(sampler.outputs["Color"],n.inputs["Color"])
                tree.links.new(n.outputs[0],principled.inputs["Normal"])
            elif prefix=="ORM":
                n=tree.nodes.new("ShaderNodeSeparateColor")
                tree.links.new(sampler.outputs["Color"],n.inputs[0])
                tree.links.new(n.outputs["Green"],principled.inputs["Roughness"])
                tree.links.new(n.outputs["Blue"],principled.inputs["Metallic"])
            elif prefix=="Emissive" and strength:
                tree.links.new(sampler.outputs["Color"],principled.inputs["Emission Color"])
            elif prefix=="BaseColor":
                n=tree.nodes.new("ShaderNodeMixRGB")
                n.blend_type="MULTIPLY"
                n.inputs[0].default_value=1
                n.inputs[2].default_value=(*color,1)
                tree.links.new(sampler.outputs["Color"],n.inputs[1])
                tree.links.new(n.outputs[0],principled.inputs["Base Color"])


def glb_materials(path):
    doc,binary=read_glb(path)
    doc["samplers"]=[{"wrapS":10497,"wrapT":10497,"magFilter":9729,"minFilter":9987}]
    image_slots={}
    for material in doc.get("materials",[]):
        key=material["name"].removeprefix("Colony_")
        color,rough,metal,family,emission=PALETTE[key]
        if not family:
            continue
        pbr=material.setdefault("pbrMetallicRoughness",{})
        maps={}
        for kind in ("BaseColor","Normal","ORM","Emissive"):
            key=family+"_"+kind
            if key not in image_slots:
                images=doc.setdefault("images",[])
                textures=doc.setdefault("textures",[])
                index=len(images)
                images.append({"name":key,"uri":"../../Textures/ColonyV4/"+key+".png"})
                image_slots[key]=len(textures)
                textures.append({"sampler":0,"source":index})
            maps[kind]={"index":image_slots[key]}
        pbr.update(baseColorTexture=maps["BaseColor"],baseColorFactor=[*color,1],
                   metallicRoughnessTexture=maps["ORM"],metallicFactor=1,roughnessFactor=1)
        material["normalTexture"]={**maps["Normal"],"scale":.54}
        material["occlusionTexture"]={**maps["ORM"],"strength":.63}
        if emission:
            material["emissiveTexture"]=maps["Emissive"]
            material["emissiveFactor"]=[emission]*3
    encoded=json.dumps(doc,ensure_ascii=False,separators=(",",":")).encode()
    encoded+=b" "*(-len(encoded)%4)
    path.write_bytes(struct.pack("<4s4I",b"glTF",2,28+len(encoded)+len(binary),len(encoded),0x4e4f534a)
                     +encoded+struct.pack("<2I",len(binary),0x004e4942)+binary)


def bounds(meshes):
    points=[v.co for o in meshes for v in o.data.vertices]
    return [[round(fn(p[i] for p in points),5) for i in range(3)] for fn in (min,max)]


def cap_for(diagonal,tier):
    zone=sum(diagonal>=limit for limit in (.5,2,8,25))
    return ((400,800,1500,3000,5000),(200,400,750,1500,2500),
            (150,150,300,600,1000),(150,150,300,600,1000))[tier][zone]


def baked_parts(geometry,collection,mats,kind,tier):
    for role,faces in geometry.sections.items():
        by_position={}
        for vertices,material in faces:
            key=tuple(sorted(tuple(round(k,7) for k in p) for p in vertices))
            if len(set(key))<3:
                continue
            if key in by_position:
                # Identical interfaces between stacked shell modules are buried.
                del by_position[key]
            else:
                by_position[key]=(vertices,material)
        geometry.sections[role]=list(by_position.values())
    chunks=objects(geometry,collection,mats)
    review={}
    for obj in chunks:
        mesh=obj.data
        mesh.set_sharp_from_angle(angle=.52)
        for face in mesh.polygons:
            face.use_smooth=True
            key=mesh.materials[face.material_index].name.removeprefix("Colony_")
            family=PALETTE[key][3]
            factor=.48 if family in {"Colony_WeatheredCladding","Interior_Bulkhead"} and kind=="Buildings" else 1.
            for li in face.loop_indices:
                mesh.uv_layers[0].data[li].uv*=factor
        name=obj.name
        obj.name=collection.name+" / "+name
        obj["asset_id"]=collection.name
        obj["part_role"]=name
        positions=bounds([obj])
        cap=cap_for(math.dist(*positions),tier)
        count=len(mesh.loop_triangles)
        assert count<=cap,(collection.name,name,count,cap)
        review[name]={"tris":count,"limit":cap}
    return chunks,review


def export(path,collection,chunk):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in chunk:
        obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(path),export_format="GLB",use_selection=True,
                             export_image_format="NONE",export_animations=False,
                             export_extras=True,export_tangents=True,export_normals=True)
    if not collection.name.endswith("ShadowProxy"):
        glb_materials(path)
    doc,binary=read_glb(path)
    n=sum(doc["accessors"][p["indices"]]["count"]//3 for m in doc["meshes"] for p in m["primitives"])
    assert n==sum(len(o.data.loop_triangles) for o in chunk),(path,n)
    assert all((path.parent/i["uri"]).is_file() for i in doc.get("images",[]))
    return n


def layer(parent,name):
    collection=bpy.data.collections.new(name)
    parent.children.link(collection)
    return collection


def emit(groups,mats,category,asset,limit,geometry):
    export_root=OUT/category
    export_root.mkdir(parents=True,exist_ok=True)
    profiles=[]
    asset_group=layer(groups,asset)
    all_parts=[]
    for tier,level in enumerate(TIERS):
        group=layer(asset_group,asset+"_"+level)
        group["tier"]=tier
        chunk,parts=baked_parts(geometry(tier),group,mats,category,tier)
        path=export_root/(asset+"_"+level+".glb")
        count=export(path,group,chunk)
        allowed=(limit,math.ceil(limit*.5),math.ceil(limit*.25),math.ceil(limit*.25))[tier]
        assert 0<count<=allowed,(asset,level,count,allowed)
        profiles.append(dict(file=path.relative_to(ROOT).as_posix(),triangles=count,budget=allowed,
                             bounds_m=bounds(chunk),parts=parts,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        all_parts.append(group)
        print(asset,level,count,flush=True)
    assert profiles[0]["triangles"]>=profiles[1]["triangles"]>=profiles[2]["triangles"],asset
    return dict(zip(TIERS,profiles)),all_parts


def gallery(name,entries,columns,spacing,path):
    scene=bpy.data.scenes.new(name)
    bpy.context.window.scene=scene
    scene.unit_settings.system="METRIC"
    for i,(id_,collection) in enumerate(entries):
        x=(i//columns-(len(entries)/columns-1)*.5)*spacing[0]
        y=(i%columns-(columns-1)*.5)*spacing[1]
        preview=bpy.data.objects.new(id_,None)
        preview.instance_type="COLLECTION"
        preview.instance_collection=collection
        preview.location=(x,y,-.012)
        scene.collection.objects.link(preview)
    camera_data=bpy.data.cameras.new(name)
    camera=bpy.data.objects.new(name,camera_data)
    scene.collection.objects.link(camera)
    extent=max(spacing[0]*len(entries)/columns,spacing[1]*columns)
    camera.location=(extent*.95,-extent*.78,extent*.93)
    focus=Vector((0,0,extent*.12))
    camera.rotation_euler=(focus-camera.location).to_track_quat("-Z","Y").to_euler()
    camera_data.type="ORTHO"
    camera_data.ortho_scale=extent*1.43
    scene.camera=camera
    floor=bpy.data.meshes.new("Review ground")
    size=extent*20
    floor.from_pydata([(-size,-size,-.035),(size,-size,-.035),(size,size,-.035),(-size,size,-.035)],[],[(0,1,2,3)])
    ground=bpy.data.objects.new("Review ground",floor)
    scene.collection.objects.link(ground)
    backdrop=bpy.data.materials.new(name+" background")
    backdrop.diffuse_color=(.123,.16,.175,1)
    ground.data.materials.append(backdrop)
    scene.world=bpy.data.worlds.new(name)
    scene.world.use_nodes=True
    scene.world.node_tree.nodes["Background"].inputs[0].default_value=(.29,.34,.39,1)
    scene.world.node_tree.nodes["Background"].inputs[1].default_value=.70
    scene.view_settings.exposure=.57
    for angle,elevation,energy in ((-.85,1.08,3.1),(1.92,.73,1.5)):
        d=bpy.data.lights.new(name,"SUN")
        d.energy=energy
        d.angle=.063
        light=bpy.data.objects.new(name,d)
        light.rotation_euler=(elevation,0,angle)
        scene.collection.objects.link(light)
    scene.render.engine="CYCLES"
    scene.cycles.samples=18
    scene.cycles.use_denoising=True
    scene.render.resolution_x,scene.render.resolution_y=1600,1100
    scene.render.resolution_percentage=100
    scene.render.filepath=str(path)
    bpy.ops.render.render(write_still=True)
    scene.render.filepath="//../Preview/ColonyV4/"+path.name
    return scene


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    work=bpy.context.scene
    work.name="Colony | library"
    work.unit_settings.system="METRIC"
    groups=layer(work.collection,"Colony V4 modular library")
    materials=make_materials()
    previous=json.loads((ROOT/"Documentation/measured_buildings.json").read_text("utf-8"))["prefabs"]
    meshes={}
    buildings={}
    props={}
    asset_roles={"new":NEW,"replacement":REPLACEMENTS,"measured_upgrade":measured_specs(previous)}
    for role,specs in asset_roles.items():
        division=layer(groups,role)
        for spec in specs:
            prior=previous.get(spec.name)
            g,port_list,front=building(spec,0,prior)
            levels,chunks=emit(division,materials,"Buildings",spec.name,spec.budget,
                               lambda tier:g if tier==0 else building(spec,tier,prior)[0])
            if spec.exact:
                for lod in levels.values():
                    low, high = lod["bounds_m"]
                    assert max(abs(a-b) for a,b in zip(
                        [high[0]-low[0],high[1]-low[1],high[2]],
                        [spec.depth,spec.width,spec.height])) < .025, (spec.name,lod["bounds_m"])
            if prior:
                ports=prior["entrances"]
            else:
                ports=[dict(kind=kind,outward_xyz=[1,0,0],threshold_xyz_m=[front,(a+b)*.5,.06],
                            opening_width_m=b-a,opening_height_m=h-.06,
                            outdoor_connection_xyz_m=[spec.depth*.5,(a+b)*.5,0],
                            keep_clear_xy_m=[[front-1.5,a],[spec.depth*.5+.25,b]])
                       for a,b,h,kind in port_list]
            buildings[spec.name]=dict(role=role,old_asset=spec.old or None,axis="+X front/+Z up",
                     dimensions_wdh_m=[spec.width,spec.depth,spec.height],foundation_z_m=levels["LOD0"]["bounds_m"][0][2],
                     floors=spec.floors,story_heights_m=prior["story_heights_m"] if prior else [spec.height/spec.floors]*spec.floors,
                     pivot_xyz_m=[0,0,0],entrances=ports,
                     mounting_sockets=({"signage_front":[front+.45,0,(port_list[0][2]+.42)]}
                                       if spec.style in {"kiosk","market","security","atrium"} else {}),
                     levels=levels)
            if prior:
                buildings[spec.name]["collision_reference"]="Documentation/measured_buildings.json"
            meshes[spec.name]=chunks[0]
    for category,(limit,names) in GROUPS.items():
        division=layer(groups,category)
        for key in names:
            name=key+"_v4" if key=="convoy_wreck" else "prop_"+key+"_v4"
            levels,chunks=emit(division,materials,"Props",name,limit,lambda tier:prop(key,tier,category))
            props[name]=dict(group=category,pivot_xyz_m=[0,0,0],levels=levels)
            meshes[name]=chunks[0]
    assert len(buildings)==38 and len(props)==26
    meta=dict(requests=["7.5","7.6.1"],units="meters",scale=1,gltf_axis="+X front/+Y up",materials="Documentation/colony_v4_textures.json",
              replacements={s.old:s.name for s in REPLACEMENTS},buildings=buildings,props=props)
    (ROOT/"Documentation/colony_v4_assets.json").write_text(json.dumps(meta,indent=2)+"\n",encoding="utf-8")
    images_for_blender(materials)
    preview=ROOT/"Preview/ColonyV4"
    preview.mkdir(parents=True,exist_ok=True)
    building_display=["Spaceport_Hangar_v4","Spaceport_ControlTower_v4","Colony_MidRise_Res_B_v4",
                      "Colony_AtmoRefinery_v4","Colony_MarketArcade_v4","Halo_Observatory_v4"]
    prop_display=["prop_wardrobe_bay_v4","prop_medpod_v4","convoy_wreck_v4",
                  "prop_vehicle_lift_v4","prop_market_booth_v4","prop_tree_erma_v4"]
    gallery("Building samples",[(n,meshes[n]) for n in building_display],3,(31,30),preview/"Buildings.png")
    gallery("Prop samples",[(n,meshes[n]) for n in prop_display],3,(4.2,4.4),preview/"Props.png")
    # Show low-detail levels only when an artist opens them in the library scene.
    def deactivate(layers):
        for item in layers.children:
            if int(item.collection.get("tier",0))>0:
                item.exclude=True
            deactivate(item)
    deactivate(work.view_layers[0].layer_collection)
    bpy.context.window.scene=bpy.data.scenes["Building samples"]
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=="VIEW_3D":
                area.spaces.active.region_3d.view_perspective="CAMERA"
                area.spaces.active.overlay.show_overlays=False
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/"Source/Colony_V4_Resources.blend"),compress=True)
    print("COLONY V4",len(buildings),len(props),flush=True)


if __name__=="__main__":
    main()
