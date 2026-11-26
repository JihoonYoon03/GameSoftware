"""Build Field Medic parts and a review assembly using the existing Mixamo master."""
import json
import math
from pathlib import Path
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import process_tripo_part as tripo
from human_layered_geometry import merge
from human_sheet_forms import FIGURES, features, head, neck

ROOT = Path(__file__).resolve().parents[1]
MASTER = ROOT / "Source/Characters/Master_Mixamo_Base.blend"
EXPORT = ROOT / "Export/HumanV4"
TIERS = ("LOD0", "LOD1", "LOD2", "ShadowProxy")

# ID, LOD0 cap, material, skin/attachment role.
BASE = {
    "Coat": (2900, "fabric", "upper"),
    "Cargo_Pants": (2000, "fabric", "lower"),
    "Boots": (1250, "rubber", "feet"),
    "Bun_Hair": (1150, "fabric", "hair"),
    "Shoulder_Guards": (750, "metal", "shoulders"),
    "Knee_Guards": (650, "metal", "knees"),
}
SKIN = {"Head_Neck": (400, "skin", "head"), "Hands": (800, "skin", "hands")}
RIGID = {
    "Forearm_Guard_L_Scanner": (400, "metal", "HandL", "LeftHand", (.620, -.045, 1.438)),
    "Forearm_Guard_R": (400, "metal", "HandR", "RightHand", (-.624, -.044, 1.431)),
    "Medical_Pouch_R": (400, "fabric", "Pelvis", "Hips", (-.155, -.100, .910)),
    "Rescue_Pack": (800, "metal", "Back", "Spine1", (0, .126, 1.251)),
    "Cartridge_Rack": (400, "metal", "Chest", "Spine2", (0, -.144, 1.232)),
}
AXES = Matrix(((0, 1, 0, 0), (-1, 0, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))
GLTF_AXES = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))


def tri_count(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def build_material(name, tint, tile, metallic, roughness):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.diffuse_color = (*tint, 1)
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    folder = ROOT / "Export/Textures" / ("HumanSheet" if tile.startswith("HumanSheet") else "Pilot")
    def texture(kind):
        node = nodes.new("ShaderNodeTexImage")
        node.image = bpy.data.images.load(str(folder / f"{tile}_{kind}.png"), check_existing=True)
        node.image.colorspace_settings.name = "sRGB" if kind == "BaseColor" else "Non-Color"
        return node
    color = nodes.new("ShaderNodeMixRGB")
    color.blend_type = "MULTIPLY"
    color.inputs[0].default_value = 1
    color.inputs[1].default_value = (*tint, 1)
    links.new(texture("BaseColor").outputs["Color"], color.inputs[2])
    links.new(color.outputs[0], bsdf.inputs["Base Color"])
    normal = nodes.new("ShaderNodeNormalMap")
    normal.inputs["Strength"].default_value = .45
    links.new(texture("Normal").outputs["Color"], normal.inputs["Color"])
    links.new(normal.outputs[0], bsdf.inputs["Normal"])
    return mat


def source_part(path, rig):
    before = {o.as_pointer() for o in bpy.data.objects}
    bpy.ops.import_scene.gltf(filepath=str(path), merge_vertices=True)
    added = {o for o in bpy.data.objects if o.as_pointer() not in before}
    meshes = [o for o in added if o.type == "MESH" and o.select_get()]
    assert len(meshes)==1, (path.name, [(o.name,o.type,o.select_get()) for o in added])
    obj, = meshes
    world = obj.matrix_world.copy()
    obj.parent = None
    obj.matrix_world = world
    for mod in list(obj.modifiers):
        if mod.type == "ARMATURE":
            obj.modifiers.remove(mod)
    for ignored in added - {obj}:
        bpy.data.objects.remove(ignored, do_unlink=True)
    if rig is not None:
        obj.parent = rig
        obj.matrix_parent_inverse = rig.matrix_world.inverted()
        obj.matrix_world = world
        obj.modifiers.new("Mixamo master skin", "ARMATURE").object = rig
    return obj


def expose(body, name, allowed, material):
    obj = body.copy()
    obj.data = body.data.copy()
    obj.name = name
    bpy.context.collection.objects.link(obj)
    mesh = bmesh.new()
    mesh.from_mesh(obj.data)
    bmesh.ops.delete(mesh, geom=[v for v in mesh.verts if not allowed(v.co)], context="VERTS")
    mesh.to_mesh(obj.data)
    mesh.free()
    obj.data.materials.clear()
    obj.data.materials.append(material)
    for face in obj.data.polygons:
        face.material_index = 0
        face.use_smooth = True
    return obj


def build_head(rig, donor, skin):
    profile = FIGURES["Female_Normal"]
    skull = head(1, profile, "skin")
    geometry = merge([(neck(1,"skin"),0,None),(skull,0,None),
                      (features(1,"bun",skull,"skin"),0,None)],weld=True)
    tints = {"skin":skin}
    for tint, color in {
        "eye":(.78,.68,.59), "iris":(.16,.10,.077), "hair":(.055,.025,.016),
        "lid_skin":(.29,.135,.085), "lip_skin":(.39,.15,.115),
    }.items():
        tints[tint] = build_material("Skin detail | "+tint,color,"HumanSheet_Skin",0,.69)
    data = bpy.data.meshes.new("Field Medic head and neck")
    data.from_pydata([co+Vector((0,-.025,0)) for co in geometry.v],[],geometry.faces)
    data.update()
    for mat in tints.values():
        data.materials.append(mat)
    for face, surface in zip(data.polygons, geometry.materials):
        face.material_index = list(tints).index(surface.tint)
        face.use_smooth = True
    obj = bpy.data.objects.new("Field Medic head and neck",data)
    bpy.context.collection.objects.link(obj)
    tripo.bind(obj,rig,donor)
    return obj


def clone(obj):
    new = obj.copy()
    new.data = obj.data.copy()
    bpy.context.collection.objects.link(new)
    return new


def budget_part(obj, cap):
    # Reduce only this item while preserving the original textured source and weights.
    for _ in range(3):
        current = tri_count(obj)
        if current <= cap:
            break
        mod = obj.modifiers.new("Per-part surface reduction", "DECIMATE")
        mod.ratio = (cap - 4) / current
        mod.use_collapse_triangulate = True
        obj.modifiers.move(len(obj.modifiers)-1, 0)
        tripo.apply_modifier(obj, mod)
    assert 0 < tri_count(obj) <= cap, (obj.name, tri_count(obj), cap)


def fix_normals_and_uv(obj):
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts)>3])
    bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.calc_area()<1e-11],context="FACES_ONLY")
    bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context="VERTS")
    bm.to_mesh(mesh)
    bm.free()
    if not mesh.uv_layers:
        uv = mesh.uv_layers.new()
        for f in mesh.polygons:
            axis = max(range(3), key=lambda i: abs(f.normal[i]))
            u, v = ((1, 2), (2, 0), (0, 1))[axis]
            for i in f.loop_indices:
                p = mesh.vertices[mesh.loops[i].vertex_index].co
                uv.data[i].uv = (p[u]*2, p[v]*2)
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active.data
    for tri in mesh.loop_triangles:
        a, b, c = [uv[i].uv.copy() for i in tri.loops]
        if abs((b-a).cross(c-a))<1e-10:
            for loop, offset in zip(tri.loops, ((0,0),(.001,0),(0,.001))):
                uv[loop].uv = a+Vector(offset)
    mesh.update()


def part_box(center, size, bevel, material):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj = bpy.context.object
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(material)
    mod = obj.modifiers.new("Machined edge", "BEVEL")
    mod.width = min(bevel, min(size)*.44)
    mod.segments = 1
    tripo.apply_modifier(obj, mod)
    return obj


def assemble_hardware(key, materials):
    shell, trim, accent = materials
    boxes = []
    def box(c, s, b, mat=shell):
        boxes.append(part_box(c, s, b, mat))
    if key == "Rescue_Pack":
        box((0,.028,0),(.366,.148,.350),.035,trim)
        box((0,.100,.008),(.335,.072,.309),.032)
        box((0,.143,-.036),(.057,.034,.115),.008,trim)
        box((0,.160,-.065),(.035,.007,.013),.003,accent)
        for x in (-.105,.105):
            box((x,.133,-.002),(.104,.018,.234),.010)
            box((x,.115,-.135),(.111,.043,.039),.009,trim)
            box((x,-.058,.043),(.069,.073,.227),.012,trim)
        box((0,.025,.177),(.106,.043,.029),.009,trim)
    elif key == "Medical_Pouch_R":
        box((0,0,0),(.173,.082,.150),.019,trim)
        box((0,-.036,.020),(.158,.048,.088),.018)
        box((0,-.050,-.032),(.051,.021,.051),.006,trim)
        box((0,-.062,-.042),(.031,.005,.011),.002,accent)
    elif key == "Cartridge_Rack":
        box((0,0,0),(.234,.055,.170),.012,trim)
        for x in (-.071,0,.071):
            box((x,-.038,-.006),(.059,.051,.128),.008)
            box((x,-.066,-.042),(.030,.006,.009),.002,accent)
    else:
        box((0,0,0),(.184,.079,.056),.012,trim)
        box((0,-.004,.026),(.164,.075,.045),.012)
        if key.endswith("Scanner"):
            box((0,0,.054),(.108,.047,.019),.007,trim)
            box((-.013,0,.065),(.055,.026,.005),.0015,accent)
    tripo.select(boxes, boxes[0])
    bpy.ops.object.join()
    obj = bpy.context.object
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.data.transform(AXES.inverted())
    return obj


def flatten(matrix):
    return [round(matrix[r][c], 8) for c in range(4) for r in range(4)]


def verify_export(path, rig_reference, budget, skinned):
    doc, accessor = tripo.read_glb(path)
    assert len(doc["meshes"]) == 1 and not doc.get("animations")
    assert len(doc.get("skins", [])) == int(skinned)
    if skinned:
        names, hierarchy, inverse = tripo.skin_contract(doc, accessor)
        original_names, original_hierarchy, original_inverse = rig_reference
        assert names == original_names and hierarchy == original_hierarchy
        assert len(inverse) == len(original_inverse)
        assert max(abs(a-b) for aa, bb in zip(inverse, original_inverse) for a, b in zip(aa, bb)) < 2e-6
    count = 0
    for primitive in doc["meshes"][0]["primitives"]:
        attrs = primitive["attributes"]
        assert {"POSITION", "NORMAL", "TEXCOORD_0", "TANGENT"} <= attrs.keys()
        if skinned:
            assert {"JOINTS_0", "WEIGHTS_0"} <= attrs.keys()
            for values in accessor(attrs["WEIGHTS_0"]):
                assert abs(sum(values)-1)<.002 and max(values)>0, (path.name, values)
        count += doc["accessors"][primitive["indices"]]["count"]//3
    assert 0 < count <= budget
    return count


def main():
    args = sys.argv[sys.argv.index("--")+1:]
    destination = Path(args[0]).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(MASTER))
    body = bpy.data.objects["MasterBody"]
    rig = body.find_armature()
    rig.animation_data_clear()
    rig.data.pose_position = "REST"
    bpy.context.view_layer.update()
    assert len(rig.data.bones) == 22
    template = EXPORT / "Field_Medic_Boots_LOD0.glb"
    reference = tripo.skin_contract(*tripo.read_glb(template))
    skin = build_material("Skin | shared", (.52,.30,.20), "HumanSheet_Skin", 0, .65)
    materials = [
        build_material("Medical shell | sand ceramic", (.72,.64,.49), "Pilot_PaintedAlloy", .12,.54),
        build_material("Medical shell | frame", (.17,.19,.17), "Pilot_BrushedSteel", .28,.62),
        build_material("Medical shell | orange stripe", (.85,.29,.065), "Pilot_PaintedAlloy", 0,.46),
    ]
    originals = {}
    for key in BASE:
        originals[key] = source_part(EXPORT / f"Field_Medic_{key}_LOD0.glb", rig)
    originals["Head_Neck"] = build_head(rig,body,skin)
    originals["Hands"] = expose(body, "Body_Exposed_Hands", lambda v: abs(v.x)>.672, skin)
    offsets = {}
    for key, row in RIGID.items():
        obj = assemble_hardware(key, materials)
        offsets[key] = Matrix.Translation(Vector(row[-1]))@AXES
        originals[key] = obj
    all_levels = {}
    for tier in TIERS:
        level = {}
        for key, original in originals.items():
            row = (BASE | SKIN | RIGID)[key]
            cap = row[0] if tier == "LOD0" else max(16, int(row[0]*{"LOD1":.445,"LOD2":.209,"ShadowProxy":.14}[tier]))
            obj = clone(original)
            if key in SKIN:
                obj.name = "Body_Head" if key == "Head_Neck" else "Body_Exposed_Hands"
            else:
                obj.name = f"Field_Medic_{key}_{tier}"
            budget_part(obj, cap)
            fix_normals_and_uv(obj)
            skin_rig = None if key in RIGID else rig
            if skin_rig is None:
                obj["socket"] = RIGID[key][2]
            filename = f"Field_Medic_{key}_{tier}.glb"
            path = destination/filename
            tripo.export_glb(path, obj, skin_rig)
            tris = verify_export(path, reference, cap, skin_rig is not None)
            level[key] = {"file": filename, "triangles": tris, "cap": cap, "skinned": skin_rig is not None}
            if tier == "LOD0" and key in RIGID:
                bone = rig.data.bones["mixamorig:"+RIGID[key][3]]
                offsets_in_bone = bone.matrix_local.inverted() @ offsets[key]
                level[key].update(socket=RIGID[key][2], bone=bone.name,
                                  socket_local_matrix_gltf=flatten(GLTF_AXES@offsets_in_bone@GLTF_AXES.inverted()))
                obj.parent = rig
                obj.parent_type = "BONE"
                obj.parent_bone = bone.name
                actual_parent = rig.matrix_world@bone.matrix_local@Matrix.Translation((0,bone.length,0))
                obj.matrix_parent_inverse = actual_parent.inverted()
                obj.matrix_basis = offsets[key]
            if tier != "LOD0":
                bpy.data.objects.remove(obj, do_unlink=True)
        all_levels[tier] = level
    for obj in originals.values():
        bpy.data.objects.remove(obj, do_unlink=True)
    body.hide_render = True
    body.hide_set(True)
    body.name = "MasterBody"
    rig.name = "Mixamo_Master_Rig"
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=="VIEW_3D":
                space=area.spaces.active
                space.region_3d.view_location=(0,0,.88)
                space.region_3d.view_distance=3.6
                space.shading.color_type="MATERIAL"
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(destination/"Field_Medic_Mixamo_Assembly.blend"), compress=True)
    record = {"levels": all_levels,
              "totals": {k:sum(p["triangles"] for p in rows.values()) for k,rows in all_levels.items()},
              "rig_bones":len(rig.data.bones)}
    assert record["totals"]["LOD0"] <= 12000
    (destination/"field_medic_delivery.json").write_text(json.dumps(record, indent=2)+"\n", encoding="utf8")
    print("FIELD_MEDIC", record["totals"])


if __name__ == "__main__":
    main()
