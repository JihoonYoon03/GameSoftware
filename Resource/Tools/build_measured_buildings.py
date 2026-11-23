"""CQ-04C2 measured building prefabs, metre units, +X entrance direction."""
from dataclasses import dataclass
from copy import deepcopy
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from part_budget_policy import REFERENCE_DIAGONAL_M, triangle_budget
from texture_pilot import ROOT, write_json


@dataclass(frozen=True)
class Building:
    id: str
    district: str
    depth: float
    width: float
    stories: int
    story_height: float
    opening: tuple
    entrance: str
    wall: str
    profile: str


BUILDINGS = (
    Building("Halo_Rowhouse", "town", 11, 8, 3, 3.0, (1.7, 2.35), "pedestrian", "Habitat_Panels", "terrace"),
    Building("Halo_ServiceShop", "town", 10, 13, 1, 4.4, (4.4, 3.35), "vehicle", "Habitat_Panels", "service"),
    Building("Halo_Clinic", "town", 13, 12, 2, 3.5, (2.8, 2.65), "pedestrian", "Ceramic", "canopy"),
    Building("Karden_Logistics", "small_city", 24, 18, 2, 4.5, (6.2, 4.0), "vehicle", "Habitat_Panels", "shed"),
    Building("Karden_TransitHall", "small_city", 19, 23, 3, 4.0, (5.2, 3.4), "pedestrian", "Ceramic", "canopy"),
    Building("Karden_Market", "small_city", 15, 24, 2, 3.7, (5.8, 3.15), "pedestrian", "Habitat_Panels", "arcade"),
    Building("Metro_MixedResidence", "large_city", 24, 28, 12, 3.25, (4.2, 2.9), "pedestrian", "Ceramic", "terrace"),
    Building("Metro_CivicCommerce", "large_city", 27, 32, 14, 3.6, (6.4, 3.15), "pedestrian", "Habitat_Panels", "stepped"),
    Building("Capital_GateAdministration", "system_capital", 35, 40, 16, 4.0, (7.2, 3.65), "pedestrian", "Ceramic", "gate"),
    Building("Capital_PublicForum", "system_capital", 37, 48, 4, 4.8, (10.0, 4.1), "pedestrian", "Habitat_Concrete", "arcade"),
    Building("Frontier_FieldWorkshop", "frontier", 12, 11, 1, 4.7, (3.8, 3.4), "vehicle", "Habitat_Panels", "service"),
    Building("Frontier_FreightDepot", "frontier", 20, 16, 2, 4.8, (6.0, 4.15), "vehicle", "Habitat_Panels", "shed"),
    Building("Frontier_DecommissionedRelay", "frontier", 10, 10, 1, 4.0, (2.4, 2.8), "pedestrian", "Ruin_Obsidian", "relay"),
    Building("Mine_AccessSupport", "mine", 15, 13, 1, 4.6, (4.4, 3.55), "vehicle", "Habitat_Concrete", "mine"),
)

WALL_DEPTH = .28
FLOOR = .06
FOUNDATION = .65


class Part:
    def __init__(self):
        self.vertices, self.faces, self.materials = [], [], []

    def prism(self, corners, material):
        start = len(self.vertices)
        self.vertices.extend(corners)
        self.faces.extend(tuple(start + i for i in face) for face in (
            (3, 2, 1, 0), (4, 5, 6, 7), (0, 1, 5, 4),
            (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)))
        self.materials.extend([material] * 6)

    def box(self, x0, x1, y0, y1, z0, z1, material):
        assert x0 < x1 and y0 < y1 and z0 < z1
        self.prism([(x,y,z) for z in (z0,z1) for x,y in ((x0,y0),(x1,y0),(x1,y1),(x0,y1))], material)

    def mesh(self, name, materials):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(self.vertices, [], self.faces)
        mesh.update()
        assert not mesh.validate()
        names = tuple(dict.fromkeys(self.materials))
        for mat in names:
            mesh.materials.append(materials[mat])
        uv = mesh.uv_layers.new(name="UVMap").data
        for polygon, mat in zip(mesh.polygons, self.materials):
            polygon.material_index = names.index(mat)
            axis = max(range(3), key=lambda i: abs(polygon.normal[i]))
            u, v = [(1,2),(0,2),(0,1)][axis]
            scale = .25 if mat.startswith("Habitat_") or mat == "Ruin_Obsidian" else .5
            for index in polygon.loop_indices:
                p = mesh.vertices[mesh.loops[index].vertex_index].co
                uv[index].uv = (p[u] * scale, p[v] * scale)
        mesh.calc_loop_triangles()
        return mesh


def layout(spec):
    d, w, h = spec.depth / 2, spec.width / 2, spec.story_height
    door, door_h = spec.opening
    half = door / 2
    # Physical slabs, shells, window panels and equipment remain addressable.
    parts = {}

    def part(name, role="static", pivot=(0,0,0)):
        row = (Part(), role, pivot)
        parts[name] = row
        return row[0]

    slab = part("Foundation", "floor_and_skirt")
    slab.box(-d, d, -w, w, -FOUNDATION, FLOOR, "Habitat_Concrete")
    openings = [(-half, half, door_h, spec.entrance)]
    if spec.entrance == "vehicle":
        openings.append((half+.48, half+2.08, 2.32, "pedestrian"))
    ramp = part("ThresholdRamp", "walk_surface")
    for a,b,_,_ in openings:
        ramp.prism([(d,a,-.10), (d+1.2,a,-.10), (d+1.2,b,-.10), (d,b,-.10),
                    (d,a,FLOOR), (d+1.2,a,0), (d+1.2,b,0), (d,b,FLOOR)], "Habitat_Concrete")
    walls = part("GroundShell", "wall_collision")
    chords = [(-d, -d+WALL_DEPTH, -w, w),
              (-d+WALL_DEPTH, d-WALL_DEPTH, -w,-w+WALL_DEPTH),
              (-d+WALL_DEPTH, d-WALL_DEPTH, w-WALL_DEPTH,w)]
    boundaries = [-w] + [end for a,b,_,_ in openings for end in (a,b)] + [w]
    chords.extend((d-WALL_DEPTH,d,a,b) for a,b in zip(boundaries[::2],boundaries[1::2]))
    for x0, x1, y0, y1 in chords:
        walls.box(x0,x1,y0,y1,FLOOR,h,spec.wall)
    for a,b,height,_ in openings:
        walls.box(d-WALL_DEPTH,d,a,b,FLOOR+height,h,spec.wall)
    frame = part("PortalFrame", "static", (d, 0, FLOOR))
    for a,b,height,_ in openings:
        for y in (a, b):
            frame.box(-.20,.26,y-.075,y+.075,0,height+.09,"Steel")
        frame.box(-.20,.26,a+.075,b-.075,height-.035,height+.095,"Steel")
        frame.box(.26,.277,a+.18,b-.18,height+.006,height+.060,
                  "Glow_Cyan" if spec.profile not in {"mine","relay","shed"} else "Glow_Amber")
    ground_glass = part("GroundWindows", "facade_glazing")
    for sign in (-1,1):
        for x in (-d*.65, 0, d*.54):
            if spec.profile not in {"mine","relay"} or x != 0:
                y = sign*w
                ground_glass.box(x-d*.095,x+d*.095,min(y,y+sign*.07)-.01,max(y,y+sign*.07)+.01,
                                 min(1.34,h*.36),min(2.22,h*.68),"Glass_Opaque")
    mullions = part("PortalFixture", "static", (d, 0, FLOOR))
    cover = min(w-.25, half + (.34 if spec.entrance == "vehicle" else 1.2))
    length = {"gate": 3.1, "arcade": 2.8, "canopy": 2.4, "shed": 2.7, "mine": 1.8}.get(spec.profile, 1.35)
    z = door_h + .20
    mullions.box(-.06,length,-cover,cover,z,z+.19,"Steel")
    mullions.box(.07,length-.19,-cover+.23,cover-.23,z-.055,z,"Glow_Cyan" if spec.profile != "relay" else "Copper")
    if spec.profile in {"gate","arcade","shed","mine"}:
        for y in (-cover+.12, cover-.12):
            mullions.box(length-.30,length-.04,y-.12,y+.12,0,z,"Steel")
    # Each stable upper floor uses a real slab-to-slab height and aligned edges.
    levels = []
    level_shapes = {}
    last_inset = 0
    for floor in range(1,spec.stories):
        inset = (sum(step for at,step in ((4,1.3),(8,2.8),(11,1.6)) if floor >= at)
                 if spec.profile == "terrace" and spec.stories > 4 else
                 1.9*(floor >= spec.stories-1) if spec.profile == "terrace" else
                 2.4*(floor >= 3) + 2.9*(floor >= spec.stories-4) if spec.profile == "stepped" else
                 3.9*(floor >= 3) + 1.9*(floor >= spec.stories-2) if spec.profile == "gate" else
                 1.2*(floor >= 1) if spec.profile in {"canopy","arcade"} else 0)
        if inset != last_inset or floor == 1:
            dd,ww = d-last_inset*.65,w-last_inset
            part(f"TerraceDeck_{floor}", "floor_and_occlusion", (0,0,floor*h)).box(
                -dd,dd,-ww,ww,-.13,.03,"Habitat_Concrete")
        last_inset = inset
        shape = f"Upper{len(level_shapes)+1}" if inset not in level_shapes else level_shapes[inset]
        if inset not in level_shapes:
            level_shapes[inset] = shape
            dd, ww = d-inset*.65, w-inset
            shell = part(shape+"_Shell")
            shell.box(-dd,dd,-ww,ww,0,h,spec.wall)
            fascia = part(shape+"_SlabBand")
            fascia.box(-dd-.10,dd+.10,-ww-.10,ww+.10,.09,.31,"Steel")
            if spec.profile == "gate":
                bays = 6
            elif spec.profile in {"terrace","stepped","arcade"}:
                bays = 5
            else:
                bays = 4
            glazing = part(shape+"_EndGlazing", "facade_glazing")
            for key in range(bays):
                y = (-ww * .76) + (key+.5) * (1.52*ww/bays)
                span = ww * .60 / bays
                for sign in (-1,1):
                    x = sign*dd
                    glazing.box(min(x,x+sign*.10)-.005,max(x,x+sign*.10)+.005,y-span,y+span,
                                h*.38,h*.80,"Glass_Opaque")
            glazing = part(shape+"_SideGlazing", "facade_glazing")
            for key in range(4):
                x = (-dd*.76) + (key+.5)*(.38*dd)
                span = dd*.14
                for sign in (-1,1):
                    y = sign*ww
                    glazing.box(x-span,x+span,min(y,y+sign*.10)-.005,max(y,y+sign*.10)+.005,
                                h*.38,h*.80,"Glass_Opaque")
        levels.append((shape, floor*h))
    upper = spec.stories*h
    roof = part("Roof", "occlusion_roof", (0,0,upper))
    final_inset = [i for i, s in level_shapes.items() if levels and s == levels[-1][0]]
    inset = max(final_inset, default=0)
    rd, rw = d-inset*.65+.10, w-inset+.10
    roof.box(-rd,rd,-rw,rw,0,.25,"Steel")
    roof.box(-rd,rd,rw-.15,rw,.25,.67,"Steel")
    roof.box(-rd,rd,-rw,-rw+.15,.25,.67,"Steel")
    roof.box(-rd,-rd+.15,-rw+.15,rw-.15,.25,.67,"Steel")
    roof.box(rd-.15,rd,-rw+.15,rw-.15,.25,.67,"Steel")
    plant = part("RoofPlant", "replaceable_equipment", (-rd*.30,rw*.25,upper+.25))
    size = 1 if spec.stories < 4 else 1.9
    plant.box(-size,size,-size*.7,size*.7,0,size*.75,"Habitat_Panels")
    plant.box(-size-.085,size+.085,-size*.76,size*.76,size*.75,size*.89,"Steel")
    for n in range(4):
        x = size * (-.66 + n*.44)
        plant.box(x-.042,x+.042,-size*.76,-size*.70,size*.15,size*.59,"Steel")
    if spec.profile in {"service","shed","mine","arcade"}:
        crown = part("RoofMonitor", "occlusion_roof", (0,0,upper+.25))
        # A raised roof monitor is a glazing/vent assembly above the slab.
        sx,sy = rd*.77,rw*.35
        height = {"service":.65,"shed":1.45,"mine":.90,"arcade":1.22}[spec.profile]
        crown.box(-sx,sx,-sy,sy,0,height,"Frame_Graphite")
        crown.prism([(-sx-.12,-sy-.22,height),(sx+.12,-sy-.22,height),
                     (sx+.12,sy+.22,height),(-sx-.12,sy+.22,height),
                     (-sx-.12,-sy-.22,height+.14),(sx+.12,-sy-.22,height+.14),
                     (sx+.12,sy+.22,height+.50),(-sx-.12,sy+.22,height+.50)],spec.wall)
        for c in (-1,1):
            y = c*sy
            crown.box(-sx*.91,sx*.91,y-.066,y+.066,height*.3,height*.82,"Glass_Opaque")
    if spec.profile == "canopy":
        lantern = part("CivicLantern", "occlusion_roof", (.12,0,upper+.25))
        lantern.box(-rd*.65,rd*.65,-rw*.44,rw*.44,0,.72,"Glass_Opaque")
        lantern.box(-rd*.70,rd*.70,-rw*.55,rw*.55,.72,.87,spec.wall)
        lantern.box(-rd*.44,rd*.44,-rw*.55-.025,-rw*.55,.74,.81,"Glow_Cyan")
    if spec.profile == "gate":
        pylon = part("GateCrown", "occlusion_roof", (rd*.15,0,upper+.25))
        for y in (-rw*.63,rw*.63):
            pylon.prism([(-2.1,y-1.15,0),(2.1,y-1.15,0),(2.1,y+1.15,0),(-2.1,y+1.15,0),
                         (-1.1,y-.82,8.1),(1.7,y-.82,8.1),(1.7,y+.82,8.1),(-1.1,y+.82,8.1)],
                        "Frame_Graphite")
            pylon.box(1.7,1.76,y-.15,y+.15,2.7,7.65,"Glow_Cyan")
        pylon.box(-1.12,1.25,-rw*.63,rw*.63,6.45,7.22,"Steel")
    if spec.profile == "stepped":
        crown = part("RoofServiceBridge", "occlusion_roof", (0,0,upper+.25))
        for y in (-rw*.55,rw*.55):
            crown.box(-rd*.34,rd*.34,y-1.1,y+1.1,0,4.7,"Frame_Graphite")
        crown.box(-rd*.43,rd*.43,-rw*.55,rw*.55,3.45,4.72,"Glass_Opaque")
    if spec.profile in {"gate","relay","stepped"}:
        stem = part("SignalSpine", "replaceable_equipment", (rd*.28,-rw*.38,upper+.25))
        mast = 3.4 if spec.profile == "relay" else 4.2
        stem.box(-.18,.18,-.18,.18,0,mast,"Steel")
        stem.box(-1.1,1.1,-.35,.35,mast-.64,mast-.39,"Steel")
        stem.box(-.16,.16,-.16,.16,mast,mast+.32,"Glow_Amber")
    return parts, levels, chords, openings


def export(path, objects):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True,
                             use_active_scene=True, export_extras=True,
                             export_tangents=True, export_animations=False)


def author(render=False):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scratch = bpy.context.scene
    master = ROOT / "Source/Erma_Resource_Library.blend"
    names = {"Habitat_Panels","Habitat_Concrete","Steel","Glass_Opaque","Glow_Cyan",
             "Glow_Amber","Ceramic","Ruin_Obsidian","Copper","Frame_Graphite"}
    with bpy.data.libraries.load(str(master), link=False) as (available, result):
        assert names <= set(available.materials)
        result.materials = sorted(names)
    materials = {name: bpy.data.materials[name] for name in names}
    out = ROOT / "Export/MeasuredBuildings"
    preview = ROOT / "Preview/MeasuredBuildings"
    out.mkdir(parents=True, exist_ok=True)
    preview.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for spec in BUILDINGS:
        parts, levels, chords, openings = layout(spec)
        scene = bpy.data.scenes.new(spec.id)
        bpy.context.window.scene = scene
        scene.unit_settings.system = "METRIC"
        assembly = bpy.data.collections.new(spec.id + "_Prefab")
        scene.collection.children.link(assembly)
        details = bpy.data.collections.new(spec.id + "_Editable_Details")
        scene.collection.children.link(details)
        part_table, instances = {}, []
        for name, (shape, role, pivot) in parts.items():
            pid = spec.id + "_" + name
            optimized = deepcopy(shape)
            mesh = optimized.mesh(pid, materials)
            diagonal = math.dist([min(v.co[k] for v in mesh.vertices) for k in range(3)],
                                 [max(v.co[k] for v in mesh.vertices) for k in range(3)])
            count, cap = len(mesh.loop_triangles), triangle_budget(diagonal, REFERENCE_DIAGONAL_M)
            assert 0 < count <= cap, (pid, count, cap)
            heights = [z for kind,z in levels if name.startswith(kind+"_")] if name.startswith("Upper") else [pivot[2]]
            for k,z in enumerate(heights):
                obj = bpy.data.objects.new(pid + (f"_F{k+1}" if name.startswith("Upper") else ""), mesh)
                assembly.objects.link(obj)
                obj.location = (pivot[0],pivot[1],z)
                obj["asset_id"], obj["role"], obj["triangle_budget"] = pid, role, cap
                instances.append(obj)
            # Retain editable full relief on major hardware; simplify fine fasteners for export.
            if name in {"PortalFrame","RoofPlant","SignalSpine"}:
                for code in range(8):
                    if name == "PortalFrame":
                        x,y,z = .268,(spec.opening[0]/2) * (-1 if code < 4 else 1), .27+(code%4)*.42
                    elif name == "RoofPlant":
                        x,y,z = -.55+code*.15,-(.70 if spec.stories < 4 else 1.33)-.018,.14
                    else:
                        x,y,z = .176,-.145+code*.038,.4
                    shape.box(x-.019,x+.019,y-.025,y+.025,z-.024,z+.024,"Copper")
                source_mesh = shape.mesh(pid+"_Detail", materials)
                source = bpy.data.objects.new(pid+"_Detail", source_mesh)
                details.objects.link(source)
                source.location = pivot
                source["target_part_id"] = pid
                part_table[pid] = {"detail_source": source.name,
                                   "detail_triangles": len(source_mesh.loop_triangles)}
            part_table.setdefault(pid, {}).update(role=role, triangles=count,
                triangle_budget=cap, slots=[m.name for m in mesh.materials])
        details.hide_render = details.hide_viewport = True
        path = out / (spec.id + ".glb")
        export(path, instances)
        d,w,h = spec.depth/2,spec.width/2,spec.story_height
        manifest[spec.id] = {
            "district": spec.district, "source_scene": scene.name, "asset": path.relative_to(ROOT).as_posix(),
            "structure_dimensions_m": [spec.depth,spec.width,spec.stories*h],
            "story_heights_m": [h]*spec.stories, "pivot_m": [0,0,0],
            "ground_contact_z_m": 0, "floor_surface_z_m": FLOOR,
            "foundation_skirt_depth_m": FOUNDATION, "floor_bounds_xy_m": [[-d,-w],[d,w]],
            "entrances": [{"kind": kind, "outward_xyz":[1,0,0],
                          "threshold_xyz_m": [d,(a+b)/2,FLOOR], "opening_width_m": b-a-.15,
                          "opening_height_m": height-.035, "ramp_run_m":1.2,
                          "outdoor_connection_xyz_m":[d+1.2,(a+b)/2,0],
                          "interior_connection_xyz_m":[d-2.8,(a+b)/2,FLOOR],
                          "keep_clear_xy_m":[[d-2.8,a+.075],[d+1.2,b-.075]]}
                          for a,b,height,kind in openings],
            "minimum_front_setback_m": 5.0 if spec.entrance == "vehicle" else 3.8,
            "collision": {"ground_wall_boxes_xyz_m": [[[a,c,FLOOR],[b,e,h]] for a,b,c,e in chords],
                          "lintel_boxes_xyz_m": [[[d-WALL_DEPTH,a,FLOOR+height],[d,b,h]]
                                                 for a,b,height,_ in openings],
                          "walk_surface_part": spec.id+"_Foundation", "upper_occlusion_begins_z_m":h},
            "parts": part_table,
            "instances": [{"part_id": o["asset_id"], "translation_m": list(o.location)} for o in instances],
        }
        if render:
            from build_texture_pilot import setup_view, aim_at
            setup_view(scene, "Building", instances)
            target = Vector((0,0,(spec.stories*h-FOUNDATION)*.5))
            offset = scene.camera.location - target
            scene.camera.location = target + Vector((-offset.y, -offset.x, offset.z))
            aim_at(scene.camera, target)
            scene.objects[scene.name+"_ReviewGround"].location.z = FOUNDATION
            scene.cycles.samples = 12
            scene.render.filepath = str(preview / (spec.id+".png"))
            bpy.ops.render.render(write_still=True)
        print("MEASURED_BUILDING", spec.id, spec.stories, len(instances), flush=True)
    bpy.data.scenes.remove(scratch)
    bpy.context.window.scene = bpy.data.scenes[BUILDINGS[0].id]
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/"Source/Measured_Buildings.blend"), compress=True)
    write_json(ROOT/"Documentation/measured_buildings.json", {
        "schema":"erma.measured_buildings.v1", "request":"GFX-02 / CQ-04C2",
        "axes_authoring":"+X front, +Y side, +Z up", "units":"m", "gltf_transform":"(x,z,-y)",
        "placement_scale":1, "retained_materials":sorted(names), "prefabs":manifest,
    })


if __name__ == "__main__":
    author(render="--render" in sys.argv)
