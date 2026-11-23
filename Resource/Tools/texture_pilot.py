"""GFX-01/03 pilot inputs, shared surface rules and GLB audit."""
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path

from verify_resource_pack import accessor, read_glb

ROOT = Path(__file__).resolve().parents[1]
TEXTURES = ROOT / "Export/Textures"
PILOT = TEXTURES / "Pilot"
PREVIEW = ROOT / "Preview/TexturePilot"
MAPS = ("BaseColor", "Normal", "ORM")
MATERIALS = {
    "Vehicle_Teal": ("Pilot_PaintedAlloy", .25, .38),
    "Steel": ("Pilot_BrushedSteel", .25, .48),
    "Alloy_Light": ("Pilot_BrushedSteel", .25, .42),
    "Fastener_Titanium": ("Pilot_BrushedSteel", .25, .42),
    "Cloth_Ochre": ("Pilot_TechnicalWeave", .50, .34),
    "Cloth_Indigo": ("Pilot_TechnicalWeave", .50, .34),
    "Road_Asphalt": ("Road_Basalt", .50, .36),
    "Walk_Paving": ("Pilot_CastPaving", .25, .44),
    "Road_Edge": ("Pilot_CastPaving", .25, .34),
}
KEEP = {"Habitat_Concrete", "Habitat_Panels", "Rock_Mine"}
NEW = ("Pilot_PaintedAlloy", "Pilot_BrushedSteel", "Pilot_TechnicalWeave", "Pilot_CastPaving")
SAMPLES = {
    "Workshop": ("Buildings/Workshop_5p4m",),
    "RoadCurve": ("RoadPlan/RoadPlan_Curve_2L_A45_R18",
                  "RoadPlan/RoadPlan_CurveMarking_2L_A45_R18",
                  "RoadPlan/RoadPlan_WalkCurve_2L_A45_R18"),
    "Coupe": ("Vehicles/Coupe",),
    "Engineer": ("Characters/Ria_Engineer",),
    "Mine": ("Environment/Ground_Mine_2m", "Modules/Mine_Support", "Modules/Mine_Lantern"),
}


def image_path(texture_set, channel):
    folder = PILOT if texture_set in NEW else TEXTURES
    return folder / f"{texture_set}_{channel}.png"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def glb_path(source):
    return ROOT / "Export" / (source + ".glb")


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def quantile(values, share):
    index = round((len(values) - 1) * share)
    return round(sorted(values)[index], 3) if values else None


def primitive_stats(doc, binary, primitive):
    attributes = {name: accessor(doc, binary, index)
                  for name, index in primitive["attributes"].items()
                  if name in {"POSITION", "TEXCOORD_0", "TANGENT", "NORMAL"}}
    vertices = attributes["POSITION"]
    uv = attributes.get("TEXCOORD_0")
    tangent = attributes.get("TANGENT")
    normals = attributes.get("NORMAL")
    indices = [i[0] for i in accessor(doc, binary, primitive["indices"])]
    densities, bad_uv = [], 0
    for a, b, c in zip(indices[::3], indices[1::3], indices[2::3]):
        if uv is None:
            bad_uv += 1
            continue
        first, second = [[values[index][k] - values[a][k] for k in range(width)]
                         for values, index, width in ((uv, b, 2), (uv, c, 2))]
        determinant = first[0] * second[1] - first[1] * second[0]
        if abs(determinant) < 1e-10:
            bad_uv += 1
            continue
        x = [vertices[b][k] - vertices[a][k] for k in range(3)]
        y = [vertices[c][k] - vertices[a][k] for k in range(3)]
        area = math.hypot(*(x[k-2]*y[k-1] - x[k-1]*y[k-2] for k in range(3)))
        if area > 1e-10:
            densities.append(math.sqrt(abs(determinant) / area))
    bad_tangent = None
    if tangent and normals:
        bad_tangent = sum(not all(math.isfinite(x) for x in t + n)
                          or abs(sum(x*x for x in t[:3])-1) > .004
                          or abs(sum(x*x for x in n)-1) > .004
                          or abs(sum(x*y for x, y in zip(t, n))) > .002
                          or abs(abs(t[3])-1) > .001 for t, n in zip(tangent, normals))
    return {
        "vertices": len(vertices), "indices": len(indices), "triangles": len(indices) // 3,
        "uv_degenerate_triangles": bad_uv, "invalid_tangents": bad_tangent,
        "negative_tangent_w": sum(t[3] < 0 for t in tangent or ()),
        "uv_repeats_per_m_p10_p50_p90": [quantile(densities, q) for q in (.1, .5, .9)],
    }


def classify(node, doc, material_names):
    extras = node.get("extras", {})
    if "skin" in node:
        names = [doc["nodes"][index]["name"] for index in doc["skins"][node["skin"]]["joints"]]
        return {"role": "skinned", "rig": extras.get("skeleton"), "joints": names,
                "socket": extras.get("attachment_socket"), "combine_candidate": False}
    if any(doc["materials"][index].get("alphaMode", "OPAQUE") != "OPAQUE"
           for index in material_names):
        return {"role": "transparency_separate", "combine_candidate": False}
    if "road_plan" in extras.get("family", ""):
        role = extras.get("collision_role") or extras.get("slot")
        return {"role": role, "combine_candidate": False}
    slot = extras.get("slot", "")
    if any(word in slot.lower() for word in ("door", "wheel", "gate")):
        return {"role": "articulated_or_attachment", "combine_candidate": False}
    return {"role": "static_surface", "combine_candidate": True}


def audit():
    catalog = json.loads((ROOT / "Documentation/asset_catalog.json").read_text(encoding="utf-8"))
    assets = {row["id"]: row for row in catalog["assets"]}
    road = json.loads((ROOT / "Documentation/road_plan_modules.json").read_text(encoding="utf-8"))
    inputs, materials = {}, {}
    for files in SAMPLES.values():
        for source in files:
            path = glb_path(source)
            doc, binary = read_glb(path)
            parents = {child: parent for parent, node in enumerate(doc["nodes"])
                       for child in node.get("children", [])}
            parts = []
            for index, node in enumerate(doc["nodes"]):
                if "mesh" not in node:
                    continue
                part = assets[node["extras"]["asset_id"]]
                prims = []
                for primitive in doc["meshes"][node["mesh"]]["primitives"]:
                    material = doc["materials"][primitive["material"]]
                    name = material["name"]
                    stats = primitive_stats(doc, binary, primitive)
                    texture_set, scale, _ = MATERIALS.get(name, (name if name in KEEP else None, 1, 0))
                    if texture_set:
                        assert stats["uv_degenerate_triangles"] == stats["invalid_tangents"] == 0, (source, part["id"], name)
                        size = 1024 if texture_set in NEW else 512
                        stats["estimated_texel_per_m_p10_p50_p90"] = [
                            round(density * scale * size, 1)
                            for density in stats["uv_repeats_per_m_p10_p50_p90"]]
                    prims.append({"slot": name, **stats})
                    pbr = material.get("pbrMetallicRoughness", {})
                    mode = material.get("alphaMode", "OPAQUE")
                    record = {
                        "texture_set": texture_set, "UV_multiplier": scale,
                        "normal_strength": MATERIALS.get(name, (None, None, 1))[2],
                        "alpha_mode": mode, "alpha_cutoff": material.get("alphaCutoff", .5) if mode == "MASK" else None,
                        "double_sided": material.get("doubleSided", False),
                        "shadow_policy_proposed": "excluded_from_opaque_shadow" if mode == "BLEND" else "opaque_coverage",
                        "source_shader_constants": {k: pbr.get(k, default) for k, default in
                                                    (("baseColorFactor", [1]*4), ("metallicFactor", 1), ("roughnessFactor", 1))},
                        "emissive_factor": material.get("emissiveFactor", [0, 0, 0]),
                        "tint_mode": "source_linear_base_factor" if texture_set in NEW else "authored",
                        "template": "world." + mode.lower(),
                    }
                    assert materials.setdefault(name, record) == record, (source, name)
                total = sum(row["triangles"] for row in prims)
                assert total == part["triangles"] <= part["triangle_budget"] <= 200
                parts.append({
                    "part_id": part["id"], "node": node["name"],
                    "parent": doc["nodes"][parents[index]]["name"] if index in parents else None,
                    "local_transform_gltf": {key: node[key] for key in ("matrix", "translation", "rotation", "scale") if key in node},
                    "render_bounds_authoring_m": part.get("render_bounds"),
                    "triangles": total, "triangle_budget": part["triangle_budget"],
                    "rig_id": part.get("skeleton"), "lod": "source_only",
                    **classify(node, doc, [p["material"] for p in doc["meshes"][node["mesh"]]["primitives"]]),
                    "primitives": prims,
                })
            module = road["modules"].get(source.split("/")[-1])
            slots = sorted({prim["slot"] for part in parts for prim in part["primitives"]})
            inputs[source] = {
                "glb": path.relative_to(ROOT).as_posix(), "sha256": digest(path),
                "parts": parts, "material_ids": slots, "source_animation_count": len(doc.get("animations", [])),
                "source_road_module": module,
                "opaque_material_ids": [name for name in slots if materials[name]["alpha_mode"] == "OPAQUE"],
            }
    return inputs, materials
