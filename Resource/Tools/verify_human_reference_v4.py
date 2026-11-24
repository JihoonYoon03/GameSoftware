"""Verify the v4 skinned part bank and, when run in Blender, its editing project."""
import hashlib
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_resource_pack import ROOT, accessor, read_glb

STAGES = ("LOD0", "LOD1", "LOD2", "ShadowProxy")
REQUIRED = {"POSITION", "NORMAL", "TANGENT", "TEXCOORD_0", "COLOR_0", "JOINTS_0", "WEIGHTS_0"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def skeleton(glb, binary):
    skin = glb["skins"][0]
    nodes = glb["nodes"]
    parent = {child: i for i, node in enumerate(nodes) for child in node.get("children", [])}
    inverse = accessor(glb, binary, skin["inverseBindMatrices"])
    joints = skin["joints"]
    return [(nodes[joint]["name"],
             nodes[parent[joint]]["name"] if parent.get(joint) in joints else None,
             matrix) for joint, matrix in zip(joints, inverse)]


def actual_bounds(positions):
    coordinates = [(x, -z, y) for x, y, z in positions]
    return [[min(dimension) if side == 0 else max(dimension) for dimension in zip(*coordinates)]
            for side in range(2)]


def part_cap(row, stage):
    diagonal = math.dist(*row["bounds_m"])
    size = sum(diagonal >= threshold for threshold in (.5, 2, 8, 25))
    return {
        "LOD0": (400, 800, 1500, 3000, 5000),
        "LOD1": (200, 400, 750, 1500, 2500),
        "LOD2": (150, 150, 300, 600, 1000),
        "ShadowProxy": (150,) * 5,
    }[stage][size]


def normalized_values(doc, binary, index):
    data = doc["accessors"][index]
    divisor = {5121: 255, 5123: 65535}[data["componentType"]] if data.get("normalized") else 1
    return [(sum(values) / divisor) for values in accessor(doc, binary, index)]


def verify_part(doc, binary, node, row, size, joint_count, socket_joint):
    assert node["name"] == row["node"] and node["skin"] == 0
    assert node["extras"]["triangle_budget"] == row["cap"] == size
    triangles, positions = 0, []
    materials = set()
    for primitive in doc["meshes"][node["mesh"]]["primitives"]:
        assert primitive.get("mode", 4) == 4
        attrs = primitive["attributes"]
        assert REQUIRED <= attrs.keys(), (node["name"], REQUIRED - attrs.keys())
        channels = {key: accessor(doc, binary, attrs[key]) for key in REQUIRED}
        pos = channels["POSITION"]
        positions.extend(pos)
        assert all(len(values) == len(pos) for values in channels.values())
        assert all(math.isfinite(v) for buffer in channels.values() for value in buffer for v in value)
        indices = [i for (i,) in accessor(doc, binary, primitive["indices"])]
        assert len(indices) % 3 == 0 and max(indices) < len(pos)
        for a, b, c in zip(*[iter(indices)] * 3):
            u = tuple(y - x for x, y in zip(pos[a], pos[b]))
            v = tuple(y - x for x, y in zip(pos[a], pos[c]))
            area = sum((u[i] * v[i - 1] - v[i] * u[i - 1]) ** 2 for i in range(3))
            assert area > 1e-22, (node["name"], a, b, c)
        triangles += len(indices) // 3
        for normal, tangent in zip(channels["NORMAL"], channels["TANGENT"]):
            assert abs(sum(v*v for v in normal) - 1) < .005
            assert abs(sum(v*v for v in tangent[:3]) - 1) < .005
            assert abs(sum(n*t for n, t in zip(normal, tangent))) < .005
            assert abs(abs(tangent[3]) - 1) < .001
        assert all(max(joints) < joint_count for joints in channels["JOINTS_0"])
        assert all(abs(w - 1) < .002 for w in normalized_values(doc, binary, attrs["WEIGHTS_0"]))
        if socket_joint is not None:
            for joints, weights in zip(channels["JOINTS_0"], channels["WEIGHTS_0"]):
                assert all(joint == socket_joint for joint, weight in zip(joints, weights) if weight > 0)
        materials.add(doc["materials"][primitive["material"]]["name"])
    assert 0 < triangles == row["triangles"] <= row["cap"], node["name"]
    assert materials == set(row["materials"]), node["name"]
    delta = max(abs(x-y) for lhs, rhs in zip(actual_bounds(positions), row["bounds_m"]) for x, y in zip(lhs, rhs))
    assert delta <= 2e-5, (node["name"], delta)


def verify_export(manifest):
    bank = ROOT / manifest["shared_part_bank"]
    assert sha(bank) == manifest["shared_part_bank_sha256"]
    assert sha(ROOT / manifest["source"]) == manifest["source_sha256"]
    for image, expected in manifest["reference_checksums"].items():
        assert sha(ROOT / "Reference/Characters" / image) == expected, image
    doc, binary = read_glb(bank)
    assert len(doc["skins"]) == 1 and not doc.get("animations") and not doc.get("extensionsRequired")
    previous = ROOT / "Export/Characters/Player_Explorer.glb"
    actual = skeleton(doc, binary)
    expected = skeleton(*read_glb(previous))
    assert len(actual) == len(expected) == manifest["bone_count"] == 23
    assert [r[:2] for r in actual] == [r[:2] for r in expected]
    bind_delta = max(abs(x-y) for (_, _, a), (_, _, b) in zip(actual, expected) for x, y in zip(a, b))
    assert bind_delta <= 2e-5

    materials = {m["name"]: m for m in doc["materials"]}
    assert set(materials) == {"HumanV4_" + name for name in ("Fabric", "Shell", "Skin", "Visor")}
    assert materials["HumanV4_Visor"]["alphaMode"] == "BLEND"
    textures = set()
    for name in ("HumanV4_Fabric", "HumanV4_Shell"):
        material = materials[name]
        pbr = material["pbrMetallicRoughness"]
        orm = pbr["metallicRoughnessTexture"]["index"]
        assert material["occlusionTexture"]["index"] == orm
        textures.update((pbr["baseColorTexture"]["index"], material["normalTexture"]["index"], orm))
    assert len(textures) == len(doc["images"]) == 6
    assert all("bufferView" in image for image in doc["images"])

    stages = manifest["part_stages"]
    catalog = json.loads((ROOT / "Documentation/asset_catalog.json").read_text(encoding="utf-8"))
    legacy = {row["id"]: row for row in catalog["assets"]}
    sockets = json.loads((ROOT / "Documentation/accessory_sockets.json").read_text(encoding="utf-8"))
    joint_ids = {entry[0]: i for i, entry in enumerate(actual)}
    equipment = manifest["remodeled_equipment_ids"]
    socket_joints = {}
    mount_deltas = []
    for part in equipment:
        slot = legacy[part]["slot"]
        socket = sockets["sockets"]["head" if slot == "hat" else slot.removeprefix("accessory.")]
        socket_joints[part] = joint_ids[socket["bone"]]
        before = legacy[part]["render_bounds"]
        after = stages["LOD0"][part]["bounds_m"]
        mount_deltas.append(max(abs((a + b - c - d) / 2)
                                  for a, b, c, d in zip(before["min"], before["max"], *after)))
    all_nodes = [node for node in doc["nodes"] if "mesh" in node]
    nodes = {node["name"]: node for node in all_nodes}
    assert len(all_nodes) == len(nodes) == sum(len(parts) for parts in stages.values())
    assert list(stages) == list(STAGES)
    rows = {}
    for stage, parts in stages.items():
        for part, row in parts.items():
            node = nodes[row["node"]]
            assert node["extras"]["asset_id"] == part and node["extras"]["lod"] == stage
            verify_part(doc, binary, node, row, part_cap(row, stage), manifest["bone_count"],
                        socket_joints.get(part))
        rows[stage] = {"part_count": len(parts), "max_part_triangles": max(r["triangles"] for r in parts.values())}
    assert set(manifest["detail_source_triangles"]) == set(stages["LOD0"])
    assert all(n >= stages["LOD0"][pid]["triangles"] for pid, n in manifest["detail_source_triangles"].items())
    assert set(manifest["remodeled_equipment_ids"]) <= set(stages["LOD0"])

    original = json.loads((ROOT / "Documentation/assembly_recipes.json").read_text(encoding="utf-8"))
    assembly_counts = {stage: [] for stage in STAGES}
    for name, char in manifest["characters"].items():
        for stage, total in char["totals"].items():
            instances = char["stages"][stage]
            counts = [stages[stage][p["part_id"]]["triangles"] for p in instances]
            assert sum(counts) == total["triangles"]
            old_triangles = sum(legacy[p]["triangles"] for p in char["optional_v3_parts"]) if stage in STAGES[:2] else 0
            assert old_triangles == total["optional_v3_triangles"]
            assert 0 < sum(counts) + old_triangles <= total["cap"]
            assembly_counts[stage].append(sum(counts) + old_triangles)
            assert all(stages[stage][p["part_id"]]["node"] == p["node"] for p in instances)
            if name in original and stage != "ShadowProxy":
                limits = original[name].get("offsets", {})
                assert all(p["offset_m"] == limits.get(p["part_id"], [0, 0, 0]) for p in instances)
                if stage == "LOD0":
                    assert set(original[name]["parts"]) == {p["part_id"] for p in instances} | set(char["optional_v3_parts"])
        assert char["skin_palette"] in manifest["material_contract"]["skin_palette_linear_rgb"]

    return {"bones_verified": len(actual), "max_inverse_bind_delta_m": bind_delta,
            "meshes_verified": len(all_nodes), "stages": rows,
            "character_assemblies_verified": len(manifest["characters"]),
            "outfit_body_combinations": math.prod(len(v) for v in manifest["outfit_variants"].values()),
            "remodeled_equipment": len(equipment),
            "max_equipment_bounds_center_delta_m": round(max(mount_deltas), 6),
            "assembly_triangles_with_optional_parts": {stage: [min(counts), max(counts)]
                                                        for stage, counts in assembly_counts.items()},
            "embedded_shared_maps": len(doc["images"])}


def verify_blender(manifest):
    import bpy

    bpy.ops.wm.open_mainfile(filepath=str(ROOT / manifest["source"]))
    detail = bpy.data.scenes[manifest["detail_source_scene"]]
    assert sum(obj.type == "MESH" for obj in detail.objects) == len(manifest["detail_source_triangles"])
    for obj in detail.objects:
        if obj.type == "MESH":
            obj.data.calc_loop_triangles()
            assert len(obj.data.loop_triangles) == manifest["detail_source_triangles"][obj["asset_id"]]

    scene = bpy.data.scenes[manifest["deformation_review_scene"]]
    bpy.context.window.scene = scene
    actions = {}
    for actor in scene.objects:
        if "review_action" not in actor:
            continue
        action = actor.animation_data.action
        assert action.name == actor["review_action"]
        meshes = [obj for obj in actor.children if obj.type == "MESH"]
        assert all(len(obj.modifiers) == 1 and obj.modifiers[0].object == actor for obj in meshes)
        first, last = action.frame_range
        frames = sorted({round(first), round((first + last) / 2), round(last)})
        max_delta = 0
        for frame in frames:
            scene.frame_set(frame)
            graph = bpy.context.evaluated_depsgraph_get()
            for obj in meshes:
                animated = obj.evaluated_get(graph)
                assert len(animated.data.vertices) == len(obj.data.vertices)
                for rest, posed in zip(obj.data.vertices, animated.data.vertices):
                    displacement = (posed.co - rest.co).length
                    assert math.isfinite(displacement) and displacement < 3, (actor.name, obj.name, frame)
                    max_delta = max(max_delta, displacement)
        assert max_delta > .02, (actor.name, action.name)
        actions[action.name] = {"frames_sampled": frames, "max_displacement_m": round(max_delta, 6),
                                "parts": len(meshes)}
    assert len(actions) == 4

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bank = ROOT / manifest["shared_part_bank"]
    result = bpy.ops.import_scene.gltf(filepath=str(bank))
    assert result == {"FINISHED"}
    objects = bpy.context.scene.objects
    armatures = [obj for obj in objects if obj.type == "ARMATURE"]
    assert len(armatures) == 1 and len(armatures[0].data.bones) == 23
    reimported = [obj for obj in objects if obj.type == "MESH" and "asset_id" in obj]
    expected = {row["node"] for stage in manifest["part_stages"].values() for row in stage.values()}
    assert len(reimported) == len(expected) and {obj.name for obj in reimported} == expected
    assert all(any(mod.type == "ARMATURE" and mod.object == armatures[0] for mod in obj.modifiers)
               for obj in reimported)
    return {"source_detail_parts": len(manifest["detail_source_triangles"]),
            "reimported_meshes": len(reimported), "motion_checks": actions}


if __name__ == "__main__":
    manifest = json.loads((ROOT / "Documentation/human_reference_v4.json").read_text(encoding="utf-8"))
    report = verify_export(manifest)
    if "--with-blender" in sys.argv:
        report["blender"] = verify_blender(manifest)
        path = ROOT / "Validation/human_reference_v4_report.json"
        path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
