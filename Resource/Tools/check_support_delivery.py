"""Check support bank bind, clip timing, and real Blender contact samples."""
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_support_animations import BANK, SCENES, PREVIEW
from texture_pilot import ROOT, digest, write_json
from verify_resource_pack import read_glb, accessor


def skin_data(file):
    doc, binary = read_glb(file)
    skin = doc["skins"][0]
    bindings = dict(zip([doc["nodes"][joint]["name"] for joint in skin["joints"]],
                        accessor(doc, binary, skin["inverseBindMatrices"])))
    hierarchy = {joint: doc["nodes"][parent]["name"] for parent, node in enumerate(doc["nodes"])
                 for joint in node.get("children", [])}
    return doc, binary, bindings, {doc["nodes"][i]["name"]: hierarchy.get(i)
                                  for i in skin["joints"] if doc["nodes"][i]["name"] != "root"}


def check_bank(author, path=BANK):
    original, _, base_bind, base_parent = skin_data(ROOT/"Export/Characters/Ria_Engineer.glb")
    doc, binary, actual_bind, actual_parent = skin_data(path)
    assert len(original["animations"]) == 98
    assert set(base_bind) == set(actual_bind) and len(actual_bind) == 23
    assert base_parent == actual_parent
    bind_error = max(abs(a-b) for joint in base_bind for a,b in zip(base_bind[joint],actual_bind[joint]))
    assert bind_error < .00002, bind_error
    assert set(author["clips"]) == {a["name"] for a in doc["animations"]}
    root_error, seam_error = 0, 0
    for animation in doc["animations"]:
        facts = author["clips"][animation["name"]]
        channels = {(doc["nodes"][channel["target"]["node"]]["name"], channel["target"]["path"]):
                    animation["samplers"][channel["sampler"]] for channel in animation["channels"]}
        assert len(channels) >= 23
        duration = max(accessor(doc,binary,sample["input"])[-1][0] for sample in channels.values())
        assert abs(duration-facts["duration_seconds"]) < 2e-6
        for (bone, target), sample in channels.items():
            values = accessor(doc,binary,sample["output"])
            assert all(math.isfinite(value) for row in values for value in row)
            if bone == "root":
                root_error = max(root_error, max(abs(a-b) for row in values for a,b in zip(row,values[0])))
            if facts["loop"]:
                seam_error = max(seam_error, max(abs(a-b) for a,b in zip(values[0],values[-1])))
    assert root_error < 2e-5 and seam_error < 2e-5, (root_error, seam_error)
    return {"preserved_body_clip_count": 98, "shared_support_clip_count": len(doc["animations"]),
            "bind_error": bind_error, "root_error": root_error, "loop_error": seam_error}


def evaluated(obj, graph):
    sample = obj.evaluated_get(graph)
    mesh = sample.to_mesh()
    vertices = [sample.matrix_world @ v.co for v in mesh.vertices]
    faces = [tuple(p.vertices) for p in mesh.polygons]
    sample.to_mesh_clear()
    return vertices, BVHTree.FromPolygons(vertices, faces)


def distance(vertices, tree):
    return min(tree.find_nearest(v)[3] for v in vertices)


def check_scenes(author):
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/"Source/Civic_Animations.blend"))
    assert bpy.context.scene.name == "37_Civic_Animation_Review"
    assert all("HUMAN_7H__"+name in bpy.data.actions for name in author["clips"])
    results = {}
    for name in SCENES:
        scene = bpy.data.scenes[name]
        bpy.context.window.scene = scene
        actor = next(o for o in scene.objects if o.type == "ARMATURE" and "_Ria" in o.name)
        assert len(actor.data.bones) == 23 and scene.camera
        parts = {o["asset_id"]: o for o in actor.children if o.type == "MESH" and o.get("asset_id")}
        hands = [parts["Body_Hand_L"], parts["Body_Hand_R"]]
        assert "Weapon_CoilPistol_35cm" not in parts
        distances, contacts, offsets, closest_vectors = [], [], [], []
        for index in range(9):
            frame = scene.frame_end * index / 8
            scene.frame_set(int(frame), subframe=frame%1)
            graph = bpy.context.evaluated_depsgraph_get()
            left, right = [evaluated(hand, graph)[0] for hand in hands]
            if name.startswith("47"):
                patient = next(o for o in scene.objects if o.type == "ARMATURE" and "Patient" in o.name)
                target = next(o for o in patient.children if o.get("asset_id","").startswith("Outfit_Explorer") and o.get("asset_id","").endswith("_Torso"))
                _, tree = evaluated(target, graph)
                distances.append([distance(points, tree) for points in (left,right)])
                closest_vectors.append([list(min((tree.find_nearest(v)[0]-v for v in points),key=lambda d:d.length))
                                        for points in (left,right)])
            elif name.startswith("48"):
                terminal = next(o for o in scene.objects if o.get("asset_id") == "Interior_Terminal")
                vertices, tree = evaluated(terminal, graph)
                keyboard = [p for p in vertices if .966 <= p.z <= .988 and -.62 <= p.y <= -.51]
                assert keyboard
                distances.append(min((p-q).length for p in right for q in keyboard))
                closest_vectors.append(list(min((q-p for p in right for q in keyboard),key=lambda d:d.length)))
            else:
                case = next(o for o in scene.objects if o.get("asset_id") == "Interior_Med_Kit")
                _, tree = evaluated(case, graph)
                distances.append([distance(points, tree) for points in (left,right)])
                evaluated_actor = actor.evaluated_get(graph)
                socket = evaluated_actor.matrix_world @ evaluated_actor.pose.bones["hand.R"].matrix @ Matrix(author["carry_case_hand_R_local_matrix"])
                offsets.append((socket.translation-case.evaluated_get(graph).matrix_world.translation).length)
                if index == 4:
                    contacts = [part for part,obj in parts.items() if part not in {"Body_Hand_L","Body_Hand_R"}
                                and "Cuff" not in part and tree.overlap(evaluated(obj, graph)[1])]
        results[name] = {"closest_samples_m": distances,
                         "closest_contact_vectors_m": closest_vectors,
                         "case_socket_max_error_m": max(offsets, default=0), "carry_body_intersections_midstep": contacts}
    return results


if __name__ == "__main__":
    author = json.loads((ROOT/"Validation/support_animation_authoring.json").read_text(encoding="utf-8"))
    assert author["all_checks_passed"] and author["bank_sha256"] == digest(BANK)
    report = check_bank(author)
    report["review"] = check_scenes(author)
    report["all_checks_passed"] = True
    write_json(ROOT/"Validation/support_animation_delivery.json", report)
    print("SUPPORT_DELIVERY_VERIFIED", json.dumps(report), flush=True)
