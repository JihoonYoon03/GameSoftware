"""Import before/after models into Blender and render with matched shot settings."""
import json
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Preview/MaterialPilot"
MANIFEST = json.loads((ROOT / "Documentation/material_pilot.json").read_text(encoding="utf-8"))
CASES = {
    "habitat": (["habitat"], (1, -1, 0.82), False),
    "curve": (["curve", "sidewalk", "paint"], (1, -1, 1.45), False),
    "ramp": (["ramp"], (0.8, -1, 1.1), False),
    "coupe": (["coupe"], (-1, -1, 0.87), False),
    "explorer": (["explorer"], (0.72, -1, 0.64), False),
    "mine": (["mine_rock", "mine_support"], (1, -1, 0.93), True),
}


def aim(obj, target):
    obj.rotation_euler = (target - obj.location).to_track_quat("-Z", "Y").to_euler()


def import_group(keys, textured):
    mesh_objects = []
    for key in keys:
        previous = set(bpy.data.objects)
        meta = MANIFEST["assets"][key]
        path = ROOT / meta["gltf" if textured else "source"]
        status = bpy.ops.import_scene.gltf(filepath=str(path))
        assert status == {"FINISHED"}, path
        objects = set(bpy.data.objects) - previous
        for obj in objects:
            obj.animation_data_clear()
        for obj in objects:
            if obj.type == "ARMATURE":
                obj.data.pose_position = "REST"
            if obj.parent is None and key == "mine_rock":
                obj.location.x += 0.85
                obj.location.y -= 0.14
            if obj.type == "MESH":
                mesh_objects.append(obj)
    assert mesh_objects
    bpy.context.view_layer.update()
    return mesh_objects


def setup_shot(objects, direction, dark):
    scene = bpy.context.scene
    depsgraph = bpy.context.evaluated_depsgraph_get()
    bounds = []
    for obj in objects:
        evaluated = obj.evaluated_get(depsgraph)
        bounds.extend(evaluated.matrix_world @ v.co for v in evaluated.data.vertices)
    mins = Vector(min(p[i] for p in bounds) for i in range(3))
    maxs = Vector(max(p[i] for p in bounds) for i in range(3))
    center = (mins + maxs) * 0.5
    span = max(maxs - mins)
    camera_data = bpy.data.cameras.new("Review Camera")
    camera = bpy.data.objects.new("Review Camera", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera.location = center + Vector(direction).normalized() * (span * 3)
    aim(camera, center)
    camera_data.type = "ORTHO"
    rotation = camera.rotation_euler.to_matrix()
    right = rotation @ Vector((1, 0, 0))
    up = rotation @ Vector((0, 1, 0))
    # Camera remains identical within each before/after shot.
    width = 2 * max(abs(right.dot(v - center)) for v in bounds)
    height = 2 * max(abs(up.dot(v - center)) for v in bounds)
    camera_data.ortho_scale = max(width, height * (640 / 430)) * 1.13
    camera_data.clip_end = max(100.0, span * 9)

    scene.world = bpy.data.worlds.new("Review Environment")
    scene.world.use_nodes = True
    background = scene.world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.23, 0.33, 0.43, 1)
    background.inputs["Strength"].default_value = 0.24 if dark else 0.48
    for name, cartesian, power, kelvin_rgb, size in [
        ("Key", (-1, -1, 2), 110 if dark else 270, (1, 0.78, 0.56) if dark else (1, 0.93, 0.82), 1.9),
        ("Fill", (1, -0.3, 1), 45 if dark else 110, (0.48, 0.73, 1), 2.5),
        ("Edge", (0.1, 1, 1.6), 62 if dark else 175, (0.62, 0.81, 1), 1.5),
    ]:
        lamp_data = bpy.data.lights.new(name, "AREA")
        lamp_data.energy = power * span * span
        lamp_data.color = kelvin_rgb
        lamp_data.shape = "DISK"
        lamp_data.size = size * span
        lamp = bpy.data.objects.new(name, lamp_data)
        scene.collection.objects.link(lamp)
        lamp.location = center + Vector(cartesian) * span
        aim(lamp, center)

    bpy.ops.mesh.primitive_plane_add(size=span * 200, location=(center.x, center.y, mins.z - span * 0.025))
    stage = bpy.context.object
    material = bpy.data.materials.new("Review Floor")
    material.use_nodes = True
    material.node_tree.nodes.get("Principled BSDF").inputs["Base Color"].default_value = (
        (0.032, 0.033, 0.036, 1) if dark else (0.14, 0.16, 0.17, 1)
    )
    stage.data.materials.append(material)
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = True
    scene.view_settings.view_transform = "AgX"
    scene.render.resolution_x = 640
    scene.render.resolution_y = 430
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    return scene


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    review = {}
    for name, (keys, direction, dark) in CASES.items():
        for textured in (False, True):
            bpy.ops.wm.read_factory_settings(use_empty=True)
            objects = import_group(keys, textured)
            if textured:
                expected = {Path(v["file"]).name for k in keys
                            for surface in MANIFEST["assets"][k]["texture_set_ids"]
                            for v in MANIFEST["texture_sets"][surface].values()}
                imported = {Path(img.filepath).name for img in bpy.data.images if img.has_data or img.size[0]}
                assert expected <= imported, (name, expected - imported)
            scene = setup_shot(objects, direction, dark)
            variant = "pilot" if textured else "source"
            filename = f"{name}_{variant}_r2.png"
            scene.render.filepath = str(OUT / filename)
            bpy.ops.render.render(write_still=True)
            shot = dict(assets=keys, filename=filename, mesh_objects=len(objects),
                        camera_position=list(scene.camera.location), ortho_width=scene.camera.data.ortho_scale)
            review[f"{name}_{variant}"] = shot
            print("MATERIAL_PILOT_RENDER", name, variant, flush=True)
        a, b = (review[f"{name}_{s}"] for s in ("source", "pilot"))
        for field in ("mesh_objects", "camera_position", "ortho_width"):
            assert a[field] == b[field], (name, field, a[field], b[field])
    (ROOT / "Validation/material_pilot_blender.json").write_text(json.dumps(
        dict(status="PASS", blender=bpy.app.version_string, render_engine="Cycles CPU", shots=review),
        indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
