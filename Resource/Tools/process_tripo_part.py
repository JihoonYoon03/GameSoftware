"""Blender 4.x: blender -b --python-exit-code 1 -P Resource/Tools/process_tripo_part.py
Edit the settings block. Input-space points are Blender Z-up after the FBX importer
has converted axes/units, before the corrections below. Run in a fresh Blender process.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
import json
import math
import os
import shutil
import struct

import bmesh
import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector
from mathutils.bvhtree import BVHTree

# ---- Part settings ----------------------------------------------------------
PART_CATEGORY = "Clothes"
EXPORT_CATEGORY = "HumanV4"
PART_NAME = "Frontier_Coat_LOD0"
PART_MODE = "SKINNED_OUTFIT"
SOCKET_NAME = ""
TARGET_TRIS = 3000
ROOT = Path(r"C:\Users\wlgns\Documents\Github\GameSoftware\Resource")
MASTER_RIG_BLEND = ROOT / "Source/Characters/Master_Mixamo_Base.blend"

# Mesh lacks intrinsic information about real size, forward direction or mounting point.
# Measured length takes priority. Without it, detect only unambiguous 100x unit errors.
MEASURED_LENGTH_M = None
ROTATION_DEG = (0, 0, 0)  # additional rotation after correcting the source forward axis
SOURCE_FORWARD = "-Y"     # Blender-world forward after FBX import, for rigid/static
EXPECTED_LENGTH_RANGE_M = None  # optional (min,max), e.g. (.25,.45) for a pack
SOCKET_PIVOT = (0, 0, 0)  # imported-world point to place at the socket origin
OUTFIT_REGION = "COAT"  # COAT, TOP, PANTS, FULL, HAIR, CAPE
# Three or more non-collinear source points keyed by target rest bone (without prefix).
# Example keys: LeftArm, RightArm, LeftHand, Hips. Overrides bounds-based fitting.
FIT_LANDMARKS = {}
DROP_OBJECTS = ()       # exact source mannequin mesh names
DROP_MATERIALS = ()     # exact mannequin / cap / hidden-interior material names
DROP_GROUPS = ()        # source vertex groups, a face must have all vertices in group
# Axis-aligned boxes in fitted Blender metres: ((xmin,ymin,zmin),(xmax,ymax,zmax)).
# Cuts cross-boundary polygons first. Use narrow front/back boxes to open skirt slits.
CUT_BOXES = ()
HEM_THICKNESS_M = 0.0    # e.g. .0015 for a single-sided cut garment
SKIN_CLEARANCE_M = .008
# Small-hole auto-repair after fitting (keeps large openings like neck/wrists/hem/slits intact).
FILL_SMALL_HOLES = True
MAX_HOLE_EDGES = 16           # max boundary edge count per hole to auto-fill
MAX_HOLE_PERIMETER_M = 0.12   # max boundary loop perimeter in fitted metres (12 cm)
SMOOTH_SHADING = True         # smooth polygons while keeping creases above SMOOTH_ANGLE_DEG crisp
SMOOTH_ANGLE_DEG = 40.0
# Source decoration group => single deform bone; masks survive decimation.
RIGID_GROUPS = {}       # e.g. {"Buckle": "mixamorig:Hips"}
ALPHA_MATERIALS = ()    # intentional hair cards / masked surfaces
EMISSIVE_MATERIALS = () # intentional lights; all other emission is disabled
# ---------------------------------------------------------------------------

PREFIX = "mixamorig:"


def select(objects, active):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = active


def bounds(points):
    points = list(points)
    return tuple(Vector([op(p[i] for p in points) for i in range(3)]) for op in (min, max))


def apply_modifier(obj, mod):
    select([obj], obj)
    bpy.ops.object.modifier_apply(modifier=mod.name)


def rest_point(rig, name):
    return rig.matrix_world @ rig.data.bones[PREFIX + name].head_local


def import_part(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path), use_custom_normals=True, ignore_leaf_bones=True)
    imported = set(bpy.data.objects) - before
    parts = sorted((o for o in imported if o.type == "MESH" and o.name not in DROP_OBJECTS),
                   key=lambda o: o.name)
    if not parts:
        raise ValueError("FBX contains no selected part meshes")
    for obj in parts:
        world = obj.matrix_world.copy()
        obj.parent = None
        obj.animation_data_clear()
        obj.modifiers.clear()
        obj.data = obj.data.copy()
        if obj.data.shape_keys:
            obj.shape_key_clear()
        obj.data.transform(world)
        obj.matrix_world = Matrix.Identity(4)
    for obj in imported.difference(parts):
        bpy.data.objects.remove(obj, do_unlink=True)
    select(parts, parts[0])
    if len(parts) > 1:
        bpy.ops.object.join()
    obj = parts[0]
    obj.name = PART_NAME
    if obj.name != PART_NAME:
        raise ValueError("Part name collides with an existing master object")
    return obj


def material_setup(obj):
    if not obj.data.materials:
        obj.data.materials.append(bpy.data.materials.new(PART_NAME + "_PBR"))
    for i, original in enumerate(list(obj.data.materials)):
        label = original.name if original else "Default"
        mat = original.copy() if original else bpy.data.materials.new(label)
        obj.data.materials[i] = mat
        mat.use_nodes = True
        nodes = mat.node_tree.nodes
        out = next((n for n in nodes if n.type == "OUTPUT_MATERIAL" and n.is_active_output), None)
        link = out.inputs["Surface"].links if out else []
        if not link or link[0].from_node.type != "BSDF_PRINCIPLED":
            raise ValueError(f"{label}: active output must use Principled BSDF")
        shader = link[0].from_node
        for socket, allowed in [("Alpha", ALPHA_MATERIALS), ("Emission Strength", EMISSIVE_MATERIALS)]:
            if label not in allowed:
                inp = shader.inputs[socket]
                for edge in list(inp.links):
                    mat.node_tree.links.remove(edge)
                inp.default_value = 1 if socket == "Alpha" else 0
        normal = shader.inputs["Normal"]
        if normal.is_linked and normal.links[0].from_node.type != "NORMAL_MAP":
            raise ValueError(f"{label}: expected tangent-space Normal Map node")
        if normal.is_linked:
            nm = normal.links[0].from_node
            if nm.space != "TANGENT":
                raise ValueError(f"{label}: expected tangent-space normals")
            color = nm.inputs['Color']
            if color.is_linked:
                tex = color.links[0].from_node
                if tex.type == 'TEX_IMAGE' and tex.image:
                    tex.image.colorspace_settings.name = 'Non-Color'
        for node in nodes:
            if node.type == "TEX_IMAGE" and node.image and node.image.source == "FILE":
                if not node.image.packed_file and not Path(bpy.path.abspath(node.image.filepath)).is_file():
                    raise FileNotFoundError(node.image.filepath)
        print(label, {k: (shader.inputs[k].links[0].from_node.name if shader.inputs[k].is_linked
                          else str(shader.inputs[k].default_value))
                      for k in ("Base Color", "Normal", "Roughness", "Metallic")})


def edit_geometry(obj, fitted=False):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    if not fitted:
        deform = bm.verts.layers.deform.active
        ids = {g.index for g in obj.vertex_groups if g.name in DROP_GROUPS}
        def drop(f):
            mat = obj.material_slots[f.material_index].material if obj.material_slots else None
            return (mat and mat.name in DROP_MATERIALS) or (deform and any(
                all(v[deform].get(i, 0) > .5 for v in f.verts) for i in ids))
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if drop(f)], context="FACES")
    else:
        for low, high in CUT_BOXES:
            if not all(a < b for a,b in zip(low, high)):
                raise ValueError("Invalid cut box")
            candidates = list(bm.faces)
            for axis in range(3):
                candidates = [f for f in candidates if min(v.co[axis] for v in f.verts) <= high[axis]
                              and max(v.co[axis] for v in f.verts) >= low[axis]]
            for axis in range(3):
                for val in (low[axis], high[axis]):
                    pt = Vector((0,0,0)); pt[axis] = val
                    normal = Vector((0,0,0)); normal[axis] = 1
                    geo = set(candidates)
                    for f in candidates:
                        geo.update(f.edges); geo.update(f.verts)
                    result = bmesh.ops.bisect_plane(bm, geom=list(geo), plane_co=pt, plane_no=normal, dist=1e-7)
                    candidates = [f for f in result["geom"] if isinstance(f, bmesh.types.BMFace)]
            remove = [f for f in candidates if all(a <= x <= b for a,x,b in zip(low, f.calc_center_median(), high))]
            bmesh.ops.delete(bm, geom=remove, context="FACES")
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=.0005)
        if FILL_SMALL_HOLES:
            repair_small_holes(bm)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        if PART_MODE == "STATIC_WORLD":
            remove_enclosed(bm)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    if not bm.faces:
        raise ValueError("Cleanup removed every face")
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def repair_small_holes(bm):
    # Trace closed boundary loops and fill only small accidental tears, preserving large openings.
    boundary_edges = [e for e in bm.edges if e.is_boundary]
    if not boundary_edges:
        return
    adj = {}
    for edge in boundary_edges:
        a, b = edge.verts
        adj.setdefault(a, []).append(edge)
        adj.setdefault(b, []).append(edge)

    visited = set()
    edges_to_fill = []
    filled_loops = 0
    skipped_loops = 0
    for start in boundary_edges:
        if start in visited:
            continue
        loop_edges = []
        edge = start
        curr = start.verts[0]
        closed = False
        while True:
            visited.add(edge)
            loop_edges.append(edge)
            nxt = edge.other_vert(curr)
            if nxt == start.verts[0] and len(loop_edges) >= 3:
                closed = True
                break
            incident = adj.get(nxt, [])
            if len(incident) != 2:
                break
            next_edge = incident[0] if incident[1] == edge else incident[1]
            if next_edge in visited:
                closed = (next_edge == start and len(loop_edges) >= 3)
                break
            curr = nxt
            edge = next_edge
        if not closed:
            skipped_loops += 1
            continue
        perimeter = sum(e.calc_length() for e in loop_edges)
        if len(loop_edges) <= MAX_HOLE_EDGES and perimeter <= MAX_HOLE_PERIMETER_M:
            edges_to_fill.extend(loop_edges)
            filled_loops += 1
        else:
            skipped_loops += 1

    if not edges_to_fill:
        print(f"Hole repair: 0 small holes filled ({skipped_loops} large opening(s) preserved)")
        return

    existing_faces = set(bm.faces)
    uv_layers = list(bm.loops.layers.uv.values())
    bmesh.ops.holes_fill(bm, edges=edges_to_fill, sides=MAX_HOLE_EDGES)
    new_faces = [f for f in bm.faces if f not in existing_faces]
    for face in new_faces:
        face.smooth = SMOOTH_SHADING
        mat_votes = {}
        for edge in face.edges:
            for other in edge.link_faces:
                if other in existing_faces:
                    mat_votes[other.material_index] = mat_votes.get(other.material_index, 0) + 1
        if mat_votes:
            face.material_index = max(mat_votes.items(), key=lambda item: item[1])[0]
        for uv_layer in uv_layers:
            for lp in face.loops:
                same_mat = [o[uv_layer].uv for o in lp.vert.link_loops
                            if o.face in existing_faces and o.face.material_index == face.material_index]
                samples = same_mat or [o[uv_layer].uv for o in lp.vert.link_loops if o.face in existing_faces]
                if samples:
                    avg = sum(samples, Vector((0.0, 0.0))) / len(samples)
                    lp[uv_layer].uv = avg
    ngons = [f for f in new_faces if len(f.verts) > 3]
    if ngons:
        bmesh.ops.triangulate(bm, faces=ngons)
    print(f"Hole repair: filled {filled_loops} small hole(s) (+{len(new_faces)} face(s)), "
          f"preserved {skipped_loops} large opening(s)")


def remove_enclosed(bm):
    # Drop disconnected geometry strictly inside a closed non-intersecting component.
    pending = set(bm.faces)
    shells = []
    while pending:
        first = pending.pop()
        stack, faces = [first], [first]
        while stack:
            for edge in stack.pop().edges:
                for f in edge.link_faces:
                    if f in pending:
                        pending.remove(f); stack.append(f); faces.append(f)
        verts = list({v for f in faces for v in f.verts})
        lookup = {v:i for i,v in enumerate(verts)}
        tree = BVHTree.FromPolygons([v.co for v in verts], [[lookup[v] for v in f.verts] for f in faces])
        shells.append((faces, verts, tree, all(e.is_manifold for f in faces for e in f.edges), bounds(v.co for v in verts)))
    remove = []
    for faces, verts, tree, _, (low, high) in shells:
        for _, _, outer, closed, (lo, hi) in shells:
            if not closed or outer == tree or not all(lo[i]<low[i]<=high[i]<hi[i] for i in range(3)):
                continue
            if outer.overlap(tree):
                continue
            pos, normal, _, dist = outer.find_nearest(verts[0].co)
            if pos is not None and dist > .0005 and (verts[0].co-pos).dot(normal)<0:
                remove.extend(faces)
                break
    bmesh.ops.delete(bm, geom=remove, context="FACES")


def calibrate(obj, rig, body):
    coords = [v.co for v in obj.data.vertices]
    rot = Euler(tuple(map(math.radians, ROTATION_DEG))).to_matrix().to_4x4()
    if not rig:
        yaw = {"+X":0, "-X":180, "+Y":-90, "-Y":90}[SOURCE_FORWARD]
        rot = rot @ Matrix.Rotation(math.radians(yaw),4,"Z")
    if rig and FIT_LANDMARKS:
        # Least-squares similarity registration (uniform scale, no reflection).
        src = np.array(list(FIT_LANDMARKS.values()), dtype=float)
        dst = np.array([list(rest_point(rig,k)) for k in FIT_LANDMARKS])
        a, b = src-src.mean(0), dst-dst.mean(0)
        if np.linalg.matrix_rank(a)<2 or np.linalg.matrix_rank(b)<2:
            raise ValueError("Fit needs at least three non-collinear landmarks")
        u, singular, vt = np.linalg.svd(a.T@b)
        sign = np.sign(np.linalg.det(vt.T@u.T))
        correction = np.diag([1,1,sign])
        rotation = vt.T@correction@u.T
        scale = (singular*np.diag(correction)).sum()/(a*a).sum()
        mat = Matrix((scale*rotation).tolist()).to_4x4()
        mat.translation = Vector(dst.mean(0))-mat.to_3x3()@Vector(src.mean(0))
        obj.data.transform(mat)
        return
    obj.data.transform(rot)
    lo, hi = bounds(coords)
    size = hi-lo
    scale = 1.
    if rig:
        hips = rest_point(rig,"Hips")
        body_lo, body_hi = bounds(body.matrix_world@v.co for v in body.data.vertices)
        head = rest_point(rig,"Head")
        shoulder = rest_point(rig,"LeftArm")
        bottom, top = {"COAT": (hips.z-.28,shoulder.z+.08), "TOP": (hips.z,shoulder.z+.07),
                       "PANTS": (body_lo.z+.07,hips.z+.07), "FULL": (body_lo.z+.03,shoulder.z+.075),
                       "HAIR": (head.z,body_hi.z+.025), "CAPE": (hips.z-.30,shoulder.z+.04)}[OUTFIT_REGION]
        scale = (top-bottom)/max(size.z,1e-8)
        target = Vector((hips.x,head.y if OUTFIT_REGION=="HAIR" else hips.y,(bottom+top)/2))
        if OUTFIT_REGION in ("COAT","TOP","FULL"):
            span = abs(rest_point(rig,"LeftHand").x-rest_point(rig,"RightHand").x)
            print("Fitted span / wrist span (m)",size.x*scale,span)
        delta = target-(lo+hi)*(.5*scale)
    else:
        if MEASURED_LENGTH_M is not None:
            if MEASURED_LENGTH_M <= 0:
                raise ValueError("Measured length must be positive")
            scale = MEASURED_LENGTH_M/max(size)
        else:
            ranges = {"accessories":(.035,1.5),"weapons":(.08,3),"vehicles":(.6,35),
                      "aircraft":(1,90),"buildings":(2,180),"props":(.02,8)}
            limits = EXPECTED_LENGTH_RANGE_M or ranges.get(PART_CATEGORY.lower())
            if limits:
                length = max(size)
                candidates = [s for s in (1.,.01,100.) if limits[0]<=length*s<=limits[1]]
                if 1. not in candidates:
                    if len(candidates)!=1:
                        raise ValueError(f"Ambiguous physical size {length} m; set MEASURED_LENGTH_M")
                    scale = candidates[0]
                    print(f"Corrected FBX unit error: {length:g} m x {scale:g}")
        pivot = rot @ Vector(SOCKET_PIVOT) if PART_MODE=="RIGID_SOCKET" else Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z))
        delta = -scale*pivot
    obj.data.transform(Matrix.Translation(delta) @ Matrix.Scale(scale,4))
    obj.data.update()


def clearance(obj, body, rig):
    if OUTFIT_REGION in ("HAIR","PANTS"):
        return
    tree = BVHTree.FromPolygons([body.matrix_world@v.co for v in body.data.vertices],
                                [p.vertices[:] for p in body.data.polygons])
    waist = rest_point(rig,"Hips").z
    for v in obj.data.vertices:
        if v.co.z <= waist:
            continue
        point, normal, _, distance = tree.find_nearest(v.co)
        gap = (v.co-point).dot(normal)
        if distance < .045 and gap < SKIN_CLEARANCE_M:
            correction = min(1.,(v.co.z-waist)/.04)
            v.co += normal*((SKIN_CLEARANCE_M-gap)*correction)
    obj.data.update()


def simplify(obj):
    if HEM_THICKNESS_M and PART_MODE=="SKINNED_OUTFIT":
        solid = obj.modifiers.new("Turned edges","SOLIDIFY")
        solid.thickness = HEM_THICKNESS_M
        solid.offset = -1
        apply_modifier(obj,solid)
    def triangles():
        obj.data.calc_loop_triangles()
        return len(obj.data.loop_triangles)
    count = triangles()
    if count>TARGET_TRIS:
        dec = obj.modifiers.new("LOD0 budget","DECIMATE")
        dec.ratio = TARGET_TRIS/count
        dec.use_collapse_triangulate = True
        apply_modifier(obj,dec)
    apply_modifier(obj,obj.modifiers.new("Game triangles","TRIANGULATE"))
    if not 0<triangles()<=TARGET_TRIS:
        raise ValueError(f"Triangle budget {triangles()}/{TARGET_TRIS}")
    if not obj.data.uv_layers:
        select([obj],obj)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(island_margin=.015)
        bpy.ops.object.mode_set(mode="OBJECT")
        print("Generated missing UV map for tangents")
    if SMOOTH_SHADING:
        apply_shading(obj)


def apply_shading(obj):
    # Blender 4.x safe smooth shading: smooth all faces and mark sharp edges above SMOOTH_ANGLE_DEG.
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    limit = math.radians(SMOOTH_ANGLE_DEG)
    for face in bm.faces:
        face.smooth = True
    for edge in bm.edges:
        if len(edge.link_faces) == 2:
            edge.smooth = edge.calc_face_angle(0.0) <= limit
        else:
            edge.smooth = True
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def bind(obj, rig, body):
    rigid_masks = {}
    for key, bone in RIGID_GROUPS.items():
        group = obj.vertex_groups.get(key)
        if group is not None:
            rigid_masks[bone] = rigid_masks.get(bone,set()) | {
                v.index for v in obj.data.vertices if any(g.group==group.index and g.weight>=.5 for g in v.groups)}
            if bone not in rig.data.bones:
                raise ValueError(f"Unknown decoration bone {bone}")
    obj.vertex_groups.clear()
    names = {b.name for b in rig.data.bones}
    for group in body.vertex_groups:
        obj.vertex_groups.new(name=group.name)
    transfer = obj.modifiers.new("Master body weights","DATA_TRANSFER")
    transfer.object = body
    transfer.use_vert_data = True
    transfer.data_types_verts = {"VGROUP_WEIGHTS"}
    transfer.vert_mapping = "POLYINTERP_NEAREST"  # UI: Nearest Face Interpolated
    transfer.layers_vgroup_select_src = "ALL"
    transfer.layers_vgroup_select_dst = "NAME"
    apply_modifier(obj,transfer)
    for group in list(obj.vertex_groups):
        if group.name not in names:
            obj.vertex_groups.remove(group)
    weights = [{obj.vertex_groups[g.group].name:g.weight for g in v.groups if g.weight>0}
               for v in obj.data.vertices]
    left = rest_point(rig,"LeftUpLeg")
    right = rest_point(rig,"RightUpLeg")
    mid = (left.x+right.x)/2
    hips = rest_point(rig,"Hips").z
    neighbors = [[] for _ in weights]
    for edge in obj.data.edges:
        a,b = edge.vertices
        neighbors[a].append(b); neighbors[b].append(a)
    def exclude_cross_leg(v, values):
        if v.co.z < hips and abs(v.co.x-mid)>1e-6:
            side = "Right" if (v.co.x-mid)*(left.x-right.x)>0 else "Left"
            for segment in ("UpLeg","Leg","Foot","ToeBase","Toe_End"):
                values.pop(PREFIX+side+segment,None)
    for v in obj.data.vertices:
        exclude_cross_leg(v, weights[v.index])
    for _ in range(2):
        updated = list(weights)
        for v in obj.data.vertices:
            i = v.index
            if v.co.z>=hips or abs(v.co.x-mid)>.06 or not neighbors[i]:
                continue
            values = {k:.6*w for k,w in weights[i].items()}
            for j in neighbors[i]:
                for k,w in weights[j].items():
                    values[k] = values.get(k,0)+.4*w/len(neighbors[i])
            exclude_cross_leg(v,values)
            updated[i] = values
        weights = updated
    for bone, indices in rigid_masks.items():
        for i in indices:
            weights[i] = {bone:1.}
    obj.vertex_groups.clear()
    for i, values in enumerate(weights):
        top = sorted(values.items(),key=lambda item:-item[1])[:4]
        kept = [(k,w) for k,w in top if w>=.01]
        total = sum(w for _,w in kept)
        if total<=0:
            raise ValueError(f"Vertex {i}: zero weight after master transfer and side filtering")
        for k,w in kept:
            group = obj.vertex_groups.get(k) or obj.vertex_groups.new(name=k)
            group.add([i],w/total,"REPLACE")
    world = obj.matrix_world.copy()
    obj.parent = rig
    obj.matrix_parent_inverse = rig.matrix_world.inverted()
    obj.matrix_world = world
    mod = obj.modifiers.new("Master skin","ARMATURE")
    mod.object = rig


def export_glb(path, obj, rig):
    select([obj,rig] if rig else [obj],obj)
    bpy.ops.export_scene.gltf(filepath=str(path),export_format="GLB",use_selection=True,
        export_yup=True,export_apply=rig is None,export_skins=rig is not None,
        export_def_bones=False,export_rest_position_armature=True,export_animations=False,
        export_normals=True,export_tangents=True,export_extras=True)


def read_glb(path):
    raw = path.read_bytes()
    magic, version, size, length, kind = struct.unpack_from("<4s4I",raw)
    if (magic,version,size,kind)!=(b"glTF",2,len(raw),0x4e4f534a):
        raise ValueError("Invalid GLB header")
    doc = json.loads(raw[20:20+length])
    buf = raw[28+length:]
    def accessor(index):
        a = doc["accessors"][index]
        view = doc["bufferViews"][a["bufferView"]]
        dtype = {5121:"u1",5123:"<u2",5125:"<u4",5126:"<f4"}[a["componentType"]]
        n = {"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4,"MAT4":16}[a["type"]]
        width = np.dtype(dtype).itemsize
        return np.ndarray((a["count"],n),dtype=dtype,buffer=buf,
            offset=view.get("byteOffset",0)+a.get("byteOffset",0),
            strides=(view.get("byteStride",width*n),width))
    return doc,accessor


def skin_contract(doc, accessor):
    skin, = doc["skins"]
    joints = skin["joints"]
    nodes = doc["nodes"]
    parent = {child:i for i,node in enumerate(nodes) for child in node.get("children",[])}
    hierarchy = []
    for j in joints:
        p = parent.get(j)
        while p is not None and p not in joints:
            p = parent.get(p)
        hierarchy.append(joints.index(p) if p is not None else -1)
    return [nodes[j]["name"] for j in joints],hierarchy,accessor(skin["inverseBindMatrices"])


def validate_glb(path, reference):
    doc,acc = read_glb(path)
    if len(doc["meshes"])!=1 or doc.get("animations") or bool(doc.get("skins"))!=bool(reference):
        raise ValueError("Unexpected mesh, animation or skin count")
    bones = max_weights = unassigned = triangles = vertices = 0
    if reference:
        names,parents,matrices = skin_contract(doc,acc)
        old_names,old_parents,old_matrices = reference
        bones = len(names)
        if not (names==old_names and parents==old_parents and bones<=71 and
                np.allclose(matrices,old_matrices,rtol=1e-6,atol=1e-6)):
            raise ValueError("Joint order, hierarchy, inverse binds or bone limit mismatch")
    for prim in doc["meshes"][0]["primitives"]:
        attr = prim["attributes"]
        if prim.get("mode",4)!=4 or not {"NORMAL","TANGENT","TEXCOORD_0"}<=attr.keys():
            raise ValueError("GLB primitive lacks required triangle/normal/tangent/UV data")
        count = len(acc(prim["indices"]))
        if count%3:
            raise ValueError("Invalid triangle index count")
        triangles += count//3
        vertices += len(acc(attr["POSITION"]))
        if reference:
            weights = acc(attr["WEIGHTS_0"]).astype(float)
            joints = acc(attr["JOINTS_0"])
            sums = weights.sum(axis=1)
            unassigned += int(np.count_nonzero(sums<=0))
            max_weights = max(max_weights,int((weights>0).sum(axis=1).max()))
            if "JOINTS_1" in attr or joints.max()>=bones or not np.allclose(sums,1,atol=.001):
                raise ValueError("Invalid exported skin weights / joints")
    if not 0<triangles<=TARGET_TRIS or unassigned or max_weights>4:
        raise ValueError("GLB validation failed")
    return triangles, vertices, bones, max_weights, unassigned


def publish(staged, destinations):
    # Validate both files first and restore the pair if either replacement fails.
    backups = []
    try:
        for src,dst in zip(staged,destinations):
            backup = src.parent/(dst.name+".previous")
            if dst.exists():
                shutil.copy2(dst,backup)
            os.replace(src,dst)
            backups.append((dst,backup))
    except OSError:
        for dst,backup in reversed(backups):
            if backup.exists():
                os.replace(backup,dst)
            else:
                dst.unlink()
        raise


def main():
    for name in (PART_NAME,PART_CATEGORY,EXPORT_CATEGORY):
        if not name or name in (".","..") or Path(name).name!=name or any(c in name for c in '/\\:'):
            raise ValueError("Use a single file or folder name for part settings")
    if PART_MODE not in ("SKINNED_OUTFIT","RIGID_SOCKET","STATIC_WORLD") or TARGET_TRIS<1:
        raise ValueError("Invalid PART_MODE or TARGET_TRIS")
    if PART_MODE=="RIGID_SOCKET" and SOCKET_NAME not in ("Head","Chest","Back","Pelvis","HandR","HandL","Muzzle"):
        raise ValueError("Invalid SOCKET_NAME")
    if not .006<=SKIN_CLEARANCE_M<=.010:
        raise ValueError("SKIN_CLEARANCE_M must be .006 to .010")
    source = ROOT/"Tripo"/PART_CATEGORY/(PART_NAME+".fbx")
    work = ROOT/"Source"/PART_CATEGORY/(PART_NAME+".blend")
    game = ROOT/"Export"/EXPORT_CATEGORY/(PART_NAME+".glb")
    if not source.is_file():
        raise FileNotFoundError(source)
    rig = body = None
    if PART_MODE=="SKINNED_OUTFIT":
        if work.resolve()==Path(MASTER_RIG_BLEND).resolve():
            raise ValueError("Output would overwrite master")
        bpy.ops.wm.open_mainfile(filepath=str(MASTER_RIG_BLEND))
        body = bpy.context.scene.objects["MasterBody"]
        rig = body.find_armature()
        if not rig or len(rig.data.bones)>71 or any(not b.name.startswith(PREFIX) for b in rig.data.bones):
            raise ValueError("MasterBody must use a Mixamo master armature of at most 71 bones")
        original_pose = rig.data.pose_position
        rig.data.pose_position = "REST"
        bpy.context.view_layer.update()
    else:
        bpy.ops.wm.read_factory_settings(use_empty=True)
    obj = import_part(source)
    edit_geometry(obj)
    calibrate(obj,rig,body)
    edit_geometry(obj,fitted=True)
    material_setup(obj)
    if rig:
        clearance(obj,body,rig)
    simplify(obj)
    if PART_MODE=="STATIC_WORLD":
        lo,hi = bounds(v.co for v in obj.data.vertices)
        obj.data.transform(Matrix.Translation(Vector((-(lo.x+hi.x)/2,-(lo.y+hi.y)/2,-lo.z))))
    if rig:
        bind(obj,rig,body)
    else:
        obj.vertex_groups.clear()
        if PART_MODE=="RIGID_SOCKET":
            obj["socket"] = SOCKET_NAME
    work.parent.mkdir(parents=True,exist_ok=True)
    game.parent.mkdir(parents=True,exist_ok=True)
    with TemporaryDirectory(dir=work.parent) as directory:
        temp = Path(directory)
        blend, glb = temp/work.name, temp/game.name
        reference = None
        if rig:
            export_glb(temp/"master-reference.glb",body,rig)
            reference = skin_contract(*read_glb(temp/"master-reference.glb"))
            if len(reference[0])!=len(rig.data.bones):
                raise ValueError("Exporter omitted master bones")
        export_glb(glb,obj,rig)
        tris,verts,bones,max_w,zero = validate_glb(glb,reference)
        if rig:
            rig.data.pose_position = original_pose
        bpy.ops.file.pack_all()
        # Make external master links absolute before moving the staged blend one level up.
        bpy.ops.file.make_paths_absolute()
        bpy.ops.wm.save_as_mainfile(filepath=str(blend),copy=True,relative_remap=False)
        publish((blend,glb),(work,game))
    lo,hi = bounds(v.co for v in obj.data.vertices)
    print(f"PASS | {PART_NAME} | triangles {tris}/{TARGET_TRIS}, GLB vertices {verts}, "
          f"bounds XYZ m {tuple(round(v,4) for v in hi-lo)}")
    if rig:
        print(f"PASS | master bones {bones}/71, max influences {max_w}/4, zero-weight vertices {zero}")
    for p in (work,game):
        print(f"{p} ({p.stat().st_size:,} bytes)")


if __name__=="__main__":
    main()
