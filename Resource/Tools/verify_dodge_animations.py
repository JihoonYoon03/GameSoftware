"""Exercise actual fitted skins, equipment layers, and authored dash contacts."""
import json
from pathlib import Path
import sys

import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_dodge_animations as pipeline
from civic_motion import Pose
from dodge_motion import DURATION, LAND, LAND_END, PUSH_END, SPECS, reference
from pistol_layer_motion import LOWER

PISTOL = "Weapon_CoilPistol_35cm"


def masked(lower, upper, weight=1):
    pose = Pose(lower.rig)
    for name in pose.basis:
        matrix = lower.basis[name]
        if name in LOWER or weight == 0:
            pose.basis[name] = matrix.copy()
        else:
            la, qa, sa = matrix.decompose()
            lb, qb, sb = upper.basis[name].decompose()
            pose.basis[name] = Matrix.LocRotScale(la.lerp(lb, weight),
                                                  qa.slerp(qb, weight),
                                                  sa.lerp(sb, weight))
    return pose


def fitted_assemblies():
    assemblies = {name: row for name, row in pipeline.recipes.items()
                  if isinstance(row, dict) and "body" in row}
    roles = {}
    for row in assemblies.values():
        roles.setdefault(row["role"], row)
    profiles = pipeline.read("Documentation/body_profiles.json")
    fittings = pipeline.read("Documentation/accessory_fitting_offsets.json")
    for body in profiles:
        for role, row in roles.items():
            assemblies[f"Fit_{body}_{role}"] = {
                **row, "body": body, "offsets": fittings[body][role],
                "parts": [part.replace(row["body"], body) for part in row["parts"]],
            }
    return assemblies


def verify():
    pipeline.asset_context()
    pipeline.load_functions("verify_civic_animations.py", "bind", "points")
    pipeline.load_functions("build_resource_pack.py", "clone_actor")
    source = bpy.data.objects["HUMAN_7H"]
    scene = bpy.data.scenes.new("Dodge_Equipment_QA")
    scene.collection.objects.link(source)
    bpy.context.window.scene = scene
    sample = pipeline.sampler(scene, source)
    tests = []
    for clip, (direction, push) in SPECS.items():
        base, entry, exit = reference(direction, push)
        for t, phase in [(0, entry), (1, exit)]:
            a, b = sample(clip, t), sample(base, phase)
            assert max(abs(a.basis[n][i][j] - b.basis[n][i][j])
                       for n in LOWER for i in range(4) for j in range(4)) < 2e-5
        for seconds in (PUSH_END, .1, LAND, LAND_END, .30):
            t = seconds / DURATION
            lower = sample(clip, t)
            carry = sample("Pistol_Carry_Run", (entry + .5 * t) % 1)
            aim = sample("Aim_Base", 0)
            for label, pose in [("unarmed", lower), ("carry", masked(lower, carry)),
                                ("aim", masked(lower, aim))]:
                tests.append((clip, seconds, label, pose))
        if direction in ("Back", "Left", "Right"):
            t = LAND / DURATION
            tests.append((clip, LAND, "reload", masked(sample(clip, t), sample("Reload_Pistol", .45))))
    results = {"characters": {}, "intersections": [], "min_z_m": 999}
    for name, row in fitted_assemblies().items():
        col = bpy.data.collections.new("Dodge_QA_" + name)
        scene.collection.children.link(col)
        parts = list(row["parts"])
        if PISTOL not in parts:
            parts.append(PISTOL)
        actor, meshes = pipeline.clone_actor(name, source, parts, col, skin=row.get("skin"),
                                             part_offsets=row.get("offsets"))
        actor.animation_data.use_nla = False
        for clip, seconds, layer, pose in tests:
            for bone in actor.pose.bones:
                bone.matrix_basis = pose.basis[bone.name]
            actor.update_tag()
            bpy.context.view_layer.update()
            vertices, trees = pipeline.points(meshes, True)
            results["min_z_m"] = min(results["min_z_m"], min(v.z for v in vertices))
            if trees["Boot_L"].overlap(trees["Boot_R"]):
                results["intersections"].append([name, clip, seconds, layer, "boots"])
            if layer == "unarmed":
                continue
            weapon = trees[PISTOL]
            for part, tree in trees.items():
                if part == PISTOL or part.startswith("Body_Hand_") or "Cuff_" in part:
                    continue
                if weapon.overlap(tree):
                    results["intersections"].append([name, clip, seconds, layer, part])
        results["characters"][name] = {"body": row["body"], "samples": len(tests)}
        for obj in meshes + [actor]:
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(col)
        if len(results["characters"]) % 8 == 0:
            print("DODGE_FITTINGS", len(results["characters"]), len(results["intersections"]), flush=True)
    results["all_checks_passed"] = not results["intersections"] and results["min_z_m"] >= -.005
    pipeline.write("Validation/dodge_equipment.json", results)
    assert results["all_checks_passed"], json.dumps(results["intersections"][:20])
    print("DODGE_EQUIPMENT_VERIFIED", len(results["characters"]), flush=True)


if __name__ == "__main__":
    verify()
