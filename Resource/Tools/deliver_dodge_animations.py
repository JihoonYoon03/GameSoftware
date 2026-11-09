"""Validate the staged projects and publish the CQ-05B clip contract."""
from pathlib import Path
import shutil
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_dodge_animations as pipeline
from dodge_motion import (DURATION, FPS, FRAMES, LAND, LAND_END, MOVE_SECONDS,
                          PUSH_END, REFERENCE_SPEED, REVISION, SPECS, direction, reference)
from stage_dodge_delivery import BRANCHES, SHOWCASE

REFERENCE_URL = "https://www.mixamo.com/#/?page=1&query=dodge&type=Motion%2CMotionPack"


def check_projects(clip_names):
    results = {}
    for path in (pipeline.MASTER, pipeline.CIVIC):
        bpy.ops.wm.open_mainfile(filepath=str(path))
        entry = bpy.context.scene.name
        old = pipeline.existing_signatures()
        old_scenes = set(bpy.data.scenes.keys())
        bpy.ops.wm.open_mainfile(filepath=str(pipeline.STAGE / path.name))
        assert bpy.context.scene.name == entry
        assert all(pipeline.action_signature(bpy.data.actions[name]) == signature
                   for name, signature in old.items())
        assert old_scenes <= set(bpy.data.scenes.keys())
        assert all(bpy.data.actions.get("HUMAN_7H__" + name) for name in clip_names)

        checks = {}
        for name, count in [(SHOWCASE, 4), (BRANCHES, 5)]:
            scene = bpy.data.scenes[name]
            bpy.context.window.scene = scene
            actors = [o for o in scene.objects if o.type == "ARMATURE" and len(o.data.bones) == 23]
            assert len(actors) == count and scene.camera
            def evaluated_pose(frame):
                scene.frame_set(frame)
                bpy.context.view_layer.update()
                graph = bpy.context.evaluated_depsgraph_get()
                return {o.name: [b.matrix.translation.copy()
                                 for b in o.evaluated_get(graph).pose.bones] for o in actors}

            start = evaluated_pose(0)
            at_step = evaluated_pose(3)
            movements = {actor: max((b - a).length for a, b in zip(joints, at_step[actor]))
                         for actor, joints in start.items()}
            assert min(movements.values()) > .015, (path, name, movements)
            if name == BRANCHES:
                at_interrupt = {o.name: o.matrix_world.translation.copy() for o in actors}
                scene.frame_set(12)
                for actor in actors:
                    delta = actor.matrix_world.translation - at_interrupt[actor.name]
                    if "CONTINUE RUN" in actor.name:
                        assert delta.y < -1.75, (actor.name, delta)
                    else:
                        assert abs(delta.x) + abs(delta.y) < 1e-5, (actor.name, delta)
            checks[name] = {"human_review_actors": count,
                            "min_review_actor_joint_movement_m": min(movements.values())}
        results[path.name] = {"entry_scene": entry, "preserved_actions": len(old),
                              "human_clips": len(clip_names), "review_scenes": checks}
    return results


def clip_contracts():
    rows = []
    for clip, (name, push) in SPECS.items():
        land = "R" if push == "L" else "L"
        base, entry, exit_phase = reference(name, push)
        markers = [
            ("dodge_push_plant", 0, {"foot": push}),
            ("dodge_push_off", PUSH_END, {"foot": push}),
            ("dodge_land_plant", LAND, {"foot": land}),
            ("dodge_move_window_end", MOVE_SECONDS, {}),
            ("locomotion_handoff", DURATION, {"target_clip": base, "target_normalized_phase": exit_phase}),
        ]
        rows.append({
            "clip": clip, "action": "HUMAN_7H__" + clip, "fps": FPS, "start": 0, "end": FRAMES,
            "duration_seconds": DURATION, "loop": False, "authoring_fps": FPS,
            "delivery_sampling_fps": 60, "revision": REVISION, "root_translation": "in_place",
            "root_yaw_degrees": 0, "layer": "full_body_locomotion",
            "armed_lower_body_mask": "human_lower_body", "direction": name,
            "direction_blender_xy": list(direction(name))[:2], "entry_contact": push,
            "landing_contact": land, "entry_lower_body_reference": base,
            "exit_lower_body_reference": base, "entry_phase": entry, "exit_phase": exit_phase,
            "phase_sync_group": "human_locomotion", "blend_in_seconds": .05,
            "blend_out_seconds": .10,
            "support_intervals": [{"foot": push, "seconds": [0, PUSH_END]},
                                  {"foot": land, "seconds": [LAND, LAND_END]}],
            "events": [{"event": event, "seconds": seconds, "phase": seconds / DURATION,
                        "visual_only": True, **extra} for event, seconds, extra in markers],
        })
    return rows


def deliver():
    for report in ("dodge_equipment", "dodge_glb_preservation", "dodge_roundtrip_report"):
        assert pipeline.read(f"Validation/{report}.json")["all_checks_passed"]
    preservation = pipeline.read("Validation/dodge_glb_preservation.json")["files"]
    assert len(preservation) == 10 and all(row["preserved_clips"] == 82 for row in preservation.values())
    meta = pipeline.read("Documentation/animation_clips.json")
    old = [row for row in meta["HUMAN_7H"] if row["clip"] not in SPECS]
    assert len(old) == 82
    rows = clip_contracts()
    all_clips = old + rows
    projects = check_projects([row["clip"] for row in all_clips])
    for path in (pipeline.MASTER, pipeline.CIVIC):
        temp = path.with_suffix(".dodge-publish.blend")
        shutil.copyfile(pipeline.STAGE / path.name, temp)
        temp.replace(path)

    meta["HUMAN_7H"] = all_clips
    pipeline.write("Documentation/animation_clips.json", meta)
    extension = {"contract": "dodge_blending.json", "new_clips": list(SPECS)}
    blend = pipeline.read("Documentation/animation_blending.json")
    blend["dodge_step"] = extension
    pipeline.write("Documentation/animation_blending.json", blend)
    civic = pipeline.read("Documentation/civic_animation_contract.json")
    civic.update(humanoid_clips=len(all_clips), human_new_clips=len(all_clips) - 15,
                 dodge_step=extension)
    pipeline.write("Documentation/civic_animation_contract.json", civic)
    pipeline.write("Documentation/dodge_blending.json", {
        "schema": "erma_dodge_step_v1", "request": "CQ-05B", "date": "2026-11-09",
        "skeleton": "HUMAN_7H", "bones": 23, "previous_human_clips_preserved": len(old),
        "total_human_clips": len(all_clips), "axes": {"forward": "-Y", "left": "+X", "up": "+Z"},
        "prototype_reference": {"movement_seconds": MOVE_SECONDS, "speed_mps": REFERENCE_SPEED,
                                "distance_m": REFERENCE_SPEED * MOVE_SECONDS,
                                "recovery_cooldown_after_move_seconds": .85},
        "selection": {
            "direction": "nearest of 8 actor-local headings; keep the accepted heading during the step",
            "foot": "nearest contact to the current cyclic phase, L=0 and R=0.5",
            "entry": "blend immediately from the evaluated pose; standing entry uses the same short blend",
            "exit": "continue the target loop at exit_phase or blend to idle if movement stops",
            "interrupt": "sample the current pose; hand off to collision, menu, hit or mount state immediately",
        },
        "layering": {
            "unarmed": "full body with weapon hidden",
            "armed": "human_lower_body with existing carry, aim or reload upper-body layer",
            "aim": "keep aim facing; select backward and sideways samples in that facing space",
        },
        "authority": {
            "actor_translation_yaw_collision_cancel_cooldown_damage": "game",
            "root_translation": False, "root_yaw": False, "events": "visual only",
        },
        "references": {
            "provider": "Mixamo", "date": "2026-11-09", "url": REFERENCE_URL,
            "method": "Observed public 3D previews; keys authored directly on HUMAN_7H.",
            "motions": [
                {"title": "Standing Dodge Forward", "observed": "knee flexion when loading the forward step"},
                {"title": "Standing Dodge Backward", "observed": "short backward foot withdrawal and recovery"},
                {"title": "Dodging Right", "observed": "pelvis leads the lateral load; torso follows"},
            ],
        },
        "review_scenes": [SHOWCASE, BRANCHES],
        "clips": {row["clip"]: row for row in rows},
    })
    pipeline.write("Validation/dodge_project_report.json",
                   {"projects": projects, "all_checks_passed": True})
    print("DODGE_DELIVERED", len(rows), "TOTAL", len(all_clips), flush=True)


if __name__ == "__main__":
    deliver()
