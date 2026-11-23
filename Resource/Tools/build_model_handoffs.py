"""Produce RoadPlan visual grouping and optional vehicle wreck authoring data."""
from collections import Counter
import json
import math
from pathlib import Path
import random

from PIL import Image

from texture_pilot import ROOT, digest, primitive_stats, write_json
from verify_resource_pack import accessor, read_glb

DOC = ROOT / "Documentation"
ASSETS = json.loads((DOC/"asset_catalog.json").read_text(encoding="utf-8"))
PARTS = {row["id"]:row for row in ASSETS["assets"]}


def road_handoff():
    source = DOC/"road_plan_modules.json"
    modules = json.loads(source.read_text(encoding="utf-8"))["modules"]
    records = {}
    roles = Counter()
    for name, module in modules.items():
        path = ROOT/"Export/RoadPlan"/(name+".glb")
        doc, binary = read_glb(path)
        rows = []
        for node in doc["nodes"]:
            if "mesh" not in node:
                continue
            extras = node["extras"]
            pid = extras["asset_id"]
            role = extras.get("collision_role", "")
            groups = []
            for index, primitive in enumerate(doc["meshes"][node["mesh"]]["primitives"]):
                mat = doc["materials"][primitive["material"]]
                stats = primitive_stats(doc, binary, primitive)
                assert stats["uv_degenerate_triangles"] == stats["invalid_tangents"] == 0, (pid,index)
                paint = mat["name"] in {"Marking_White","Marking_Center"}
                grouping = "marking" if paint else "transparent" if mat.get("alphaMode","OPAQUE") != "OPAQUE" else role or module["category"]
                roles[grouping] += 1
                row = {"primitive":index, "material":mat["name"], "group":grouping, **stats}
                if paint:
                    positions = accessor(doc,binary,primitive["attributes"]["POSITION"])
                    normals = accessor(doc,binary,primitive["attributes"]["NORMAL"])
                    indices = accessor(doc,binary,primitive["indices"])
                    top = []
                    for start in range(0,len(indices),3):
                        a,b,c = [positions[row[0]] for row in indices[start:start+3]]
                        upward = (b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2])
                        if upward > 1e-9 and sum(normals[row[0]][1] for row in indices[start:start+3]) > 0:
                            top.append(start//3)
                    assert top, pid
                    row["top_triangle_indices_zero_based"] = top
                groups.append(row)
            rows.append({"part_id":pid,"slot":extras.get("slot"),"part_role":role,
                         "node_transform":{key:node[key] for key in ("translation","rotation","scale","matrix") if key in node},
                         "primitive_groups":groups})
        assert len(rows) == len(module["parts"]) and {row["part_id"] for row in rows} == set(module["parts"])
        records[name] = {"glb":path.relative_to(ROOT).as_posix(),"sha256":digest(path),
                         "category":module["category"],"uv_policy":module.get("uv_policy"),
                         "parts":rows,"portal_count":len(module["ports"])}
    write_json(DOC/"road_render_handoff.json", {
        "schema":"erma.road_render_handoff.v1", "request":"GFX-05",
        "source_contract":"Documentation/road_plan_modules.json", "source_contract_sha256":digest(source),
        "module_count":len(records), "groups":dict(roles), "modules":records,
        "coordinates":"source metres; glTF +Y up",
        "placement":"keep the original positions. Current +0.006 world marking lift remains owned by the consumer.",
        "group_policy":"Separate paint, sidewalks, structure and surface keys. Sort by pass/material/LOD/shadow within spatial cells.",
        "paint_top":"Indices refer to original triangles per primitive; authoring offset is zero. Retain the lower prism in original GLB.",
        "collision":"Read ports, level, companions and collision envelopes only from the source contract.",
    })
    return len(records)


def shared_damage_mask():
    out = ROOT/"Export/Textures/SharedDamage"
    out.mkdir(parents=True, exist_ok=True)
    radius = 256
    rng = random.Random(48531)
    phases = [rng.uniform(0,math.tau) for _ in range(5)]
    pixels = bytearray()
    for j in range(radius):
        for i in range(radius):
            u,v = math.tau*i/radius, math.tau*j/radius
            pattern = .5+.25*math.sin(2*u-3*v+phases[0])+.14*math.cos(7*u+5*v+phases[1])
            texture = .5+.25*math.sin(17*u-43*v+phases[2])+.18*math.cos(49*u+31*v+phases[3])
            chips = max(0, (pattern*texture-.27)*2.1)
            soot = .52*pattern+.3*(.5+.5*math.sin(5*u+4*v+phases[4]))+.1*texture
            scratches = max(0, .63*texture+.3*pattern-.55)*2.3
            pixels.extend(round(255*min(1,max(0,x))) for x in (chips,soot,scratches))
    filename = out/"Vehicle_DamageMask.png"
    image = Image.frombytes("RGB",(radius,radius),bytes(pixels))
    image.save(filename)
    assert all(high-low>60 for low,high in image.getextrema())
    return filename


def wreck_handoff():
    mask = shared_damage_mask()
    records = {}
    for category in ("vehicle","aircraft"):
        assemblies = json.loads((DOC/(category+"_assemblies.json")).read_text(encoding="utf-8"))["types"]
        for kind, prefab in assemblies.items():
            root = "Vehicles" if category == "vehicle" else "Aircraft"
            records[kind] = {}
            for filename in (kind+".glb",kind+"_Variant.glb"):
                path = ROOT/"Export"/root/filename
                doc, binary = read_glb(path)
                rows = []
                for node in doc["nodes"]:
                    if "mesh" not in node:
                        continue
                    pid = node["extras"]["asset_id"]
                    slot = PARTS[pid]["slot"]
                    joints = set()
                    mesh = doc["meshes"][node["mesh"]]
                    skin = doc["skins"][node["skin"]]
                    for primitive in mesh["primitives"]:
                        attributes = primitive["attributes"]
                        for joint, weights in zip(accessor(doc,binary,attributes["JOINTS_0"]),
                                                  accessor(doc,binary,attributes["WEIGHTS_0"])):
                            joints.update(j for j,w in zip(joint,weights) if w > 0)
                    joint_rows = [doc["nodes"][skin["joints"][index]] for index in sorted(joints)]
                    fragment = len(joints)==1 and not any(s in slot.lower() for s in ("seat","chassis","hull","glazing","underbody"))
                    mask_slots = [doc["materials"][p["material"]]["name"] for p in mesh["primitives"]
                                  if doc["materials"][p["material"]].get("alphaMode","OPAQUE")=="OPAQUE"
                                  and not any(doc["materials"][p["material"]].get("emissiveFactor",[]))]
                    if "glass" in slot.lower() or "glazing" in slot.lower():
                        mask_slots = []
                    inverse = accessor(doc,binary,skin["inverseBindMatrices"])
                    rows.append({"part_id":pid,"slot":slot,"opaque_damage_materials":mask_slots,
                                 "optional_existing_fragment":fragment,
                                 "joint_bindings":[{"joint":joint["name"],"gltf_inverse_bind_column_major":inverse[index]}
                                                   for index,joint in zip(sorted(joints),joint_rows)]})
                records[kind][filename] = {"source":path.relative_to(ROOT).as_posix(),"rig":prefab["skeleton"],
                                           "sha256":digest(path),"parts":rows}
    write_json(DOC/"vehicle_damage_handoff.json", {
        "schema":"erma.vehicle_damage_handoff.v1","request":"GFX-08","assemblies":records,
        "mask":{"id":"erma.vehicle.shared_damage","path":mask.relative_to(ROOT).as_posix(),
                "sha256":digest(mask),"channels":{"R":"coat_chips","G":"soot","B":"scuffs"},
                "colour_space":"linear","sampler":"repeat","uv":"existing TEXCOORD_0",
                "size":[256,256],"full_mip_rgba8_bytes":349524},
        "state_parameters":{"intact":0.0,"damaged":"game fraction 0..1","wreck":1.0},
        "binding":"Reuse original assembly geometry and joints. Invert inverse bind to obtain the original pivot matrix in skin space.",
        "fragments":"Only optional rigid candidates. Runtime selects actual fractures, lifetime and count after profiling.",
    })
    return sum(len(v) for v in records.values())


if __name__ == "__main__":
    print("MODEL_HANDOFFS", road_handoff(), "road modules,", wreck_handoff(), "vehicle prefab states")
