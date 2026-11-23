"""Generate shared tile sources and matched Blender review scenes.

Run with Blender in background mode. Add -- --render for PNG comparisons.
"""
import math
from pathlib import Path
import struct
import sys
import zlib

import bpy
from mathutils import Vector
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from texture_pilot import (MAPS, MATERIALS, NEW, PILOT, PREVIEW, ROOT, SAMPLES,
                           audit, glb_path, image_path)

SIZE = 1024


def png(path, rgb):
    """RGB8 PNG. Metadata declares colour interpretation in the material contract."""
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data)))

    rgb = np.rint(np.clip(rgb, 0, 1) * 255).astype(np.uint8)
    rows = b"".join(b"\0" + row.tobytes() for row in rgb)
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">2I5B", SIZE, SIZE, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows, 7)) + chunk(b"IEND", b""))


def shared_tiles():
    PILOT.mkdir(parents=True, exist_ok=True)
    y, x = np.mgrid[:SIZE, :SIZE].astype(np.float32) / SIZE

    def wave(u, v, phase=0):
        return np.sin(math.tau * (u * x + v * y) + phase)

    rng = np.random.default_rng(22011)
    grain = rng.normal(0, 1, (SIZE, SIZE)).astype(np.float32)
    fines = (grain + np.roll(grain, 1, 0) + np.roll(grain, 1, 1)) / 3
    clouds = .54*wave(3, 2) + .28*wave(11, -7, 1.7) + .18*wave(26, 31, .4)
    brushing = .52*wave(1, 74) + .30*wave(-2, 181, .9) + .18*wave(1, 217, 1.9)
    warp, weft = wave(96, 0), wave(0, 96, 1.2)
    weave = .62*warp*weft + .24*(warp+weft)*wave(48, 48) + .14*clouds
    frequency = np.fft.fftfreq(SIZE)[None, :]
    radius_sq = frequency**2 + frequency.T**2
    spectrum = np.fft.fft2(grain)
    mottles = np.fft.ifft2(spectrum * np.exp(-radius_sq / .0005)).real
    aggregate = np.fft.ifft2(spectrum * np.exp(-radius_sq / .023)).real
    mortar = .60*np.tanh(mottles / mottles.std()) + .40*np.tanh(aggregate / aggregate.std())
    layers = [
        (NEW[0], .96 + .024*clouds + .011*fines, .44 + .045*clouds,
         .005*clouds + .0017*fines, 0.0),
        (NEW[1], .94 + .020*clouds + .020*brushing, .37 + .048*brushing + .025*clouds,
         .023*brushing + .004*fines, .85),
        (NEW[2], .90 + .043*weave + .013*clouds, .87 + .052*weave,
         .062*weave + .009*fines, 0.0),
        (NEW[3], .95 + .072*mortar, .82 + .083*mortar,
         .16*mortar + .022*fines, 0.0),
    ]
    for name, value, roughness, height, metallic in layers:
        dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * .5
        dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * .5
        normal = np.stack((-dx, dy, np.ones_like(height)), axis=-1)
        normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
        png(image_path(name, "BaseColor"), np.stack((value, value, value), axis=-1))
        png(image_path(name, "Normal"), normal * .5 + .5)
        png(image_path(name, "ORM"), np.stack((np.ones_like(height), roughness,
                                                np.full_like(height, metallic)), axis=-1))


def import_reference(path, scene):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(glb_path(path)))
    objects = sorted(set(bpy.data.objects) - before, key=lambda obj: obj.name)
    for obj in objects:
        if obj.type == "ARMATURE" and obj.animation_data:
            if path.endswith("/Ria_Engineer"):
                idle = next(action for action in bpy.data.actions if action.name == "Idle")
                obj.animation_data.action = idle
                obj.animation_data.action_slot = idle.slots[0]
                obj.animation_data.use_nla = False
                scene.frame_set(0)
                bpy.context.view_layer.update()
                bases = {bone.name: bone.matrix_basis.copy() for bone in obj.pose.bones}
                obj.animation_data_clear()
                for bone in obj.pose.bones:
                    bone.matrix_basis = bases[bone.name]
            else:
                obj.animation_data_clear()
                for bone in obj.pose.bones:
                    bone.matrix_basis.identity()
    return objects


def override(material, name, uv_name):
    textures, scale, strength = MATERIALS[name]
    result = material.copy()
    result.name = "Pilot_" + name
    nodes, links = result.node_tree.nodes, result.node_tree.links
    shader = next(node for node in nodes if node.type == "BSDF_PRINCIPLED")
    base = shader.inputs["Base Color"].default_value[:]
    uv = nodes.new("ShaderNodeUVMap")
    uv.uv_map = uv_name
    uv.location = (-800, 90)
    multiplier = nodes.new("ShaderNodeVectorMath")
    multiplier.operation = "SCALE"
    multiplier.inputs["Scale"].default_value = scale
    multiplier.location = (-610, 90)
    links.new(uv.outputs["UV"], multiplier.inputs[0])
    maps = {}
    for index, channel in enumerate(MAPS):
        image = bpy.data.images.load(str(image_path(textures, channel)), check_existing=True)
        image.colorspace_settings.name = "sRGB" if channel == "BaseColor" else "Non-Color"
        node = nodes.new("ShaderNodeTexImage")
        node.image = image
        node.extension = "REPEAT"
        node.interpolation = "Linear"
        node.label = channel
        node.location = (-395, 350 - 255*index)
        links.new(multiplier.outputs[0], node.inputs["Vector"])
        maps[channel] = node
    if textures in NEW:
        tint = nodes.new("ShaderNodeMixRGB")
        tint.blend_type = "MULTIPLY"
        tint.inputs[0].default_value = 1
        tint.inputs[2].default_value = base
        tint.location = (-110, 275)
        links.new(maps["BaseColor"].outputs["Color"], tint.inputs[1])
        links.new(tint.outputs[0], shader.inputs["Base Color"])
    else:
        links.new(maps["BaseColor"].outputs["Color"], shader.inputs["Base Color"])
    normal = nodes.new("ShaderNodeNormalMap")
    normal.uv_map = uv_name
    normal.inputs["Strength"].default_value = strength
    normal.location = (-95, 10)
    links.new(maps["Normal"].outputs["Color"], normal.inputs["Color"])
    links.new(normal.outputs[0], shader.inputs["Normal"])
    orm = nodes.new("ShaderNodeSeparateColor")
    orm.location = (-155, -210)
    links.new(maps["ORM"].outputs["Color"], orm.inputs[0])
    links.new(orm.outputs["Green"], shader.inputs["Roughness"])
    links.new(orm.outputs["Blue"], shader.inputs["Metallic"])
    result["texture_set"] = textures
    result["uv_multiplier"] = scale
    return result


def aim_at(obj, target):
    obj.rotation_euler = (target - obj.location).to_track_quat("-Z", "Y").to_euler()


def setup_view(scene, sample, objects):
    mesh_objects = [obj for obj in objects if obj.type == "MESH"]
    graph = bpy.context.evaluated_depsgraph_get()
    evaluated = [obj.evaluated_get(graph) for obj in mesh_objects]
    world_points = [obj.matrix_world @ vertex.co for obj in evaluated for vertex in obj.data.vertices]
    low = Vector(tuple(min(p[k] for p in world_points) for k in range(3)))
    high = Vector(tuple(max(p[k] for p in world_points) for k in range(3)))
    target = (low + high) * .5
    size = max(high.x - low.x, high.y - low.y, high.z - low.z)
    world = bpy.data.worlds.new(scene.name + "_World")
    world.use_nodes = True
    dark = sample == "Mine"
    world.node_tree.nodes["Background"].inputs[0].default_value = (.20, .25, .34, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = .17 if dark else .48
    scene.world = world
    camera = bpy.data.objects.new(scene.name + "_Camera", bpy.data.cameras.new(scene.name))
    scene.collection.objects.link(camera)
    offset = Vector((.9, -1.65, 1.85 if sample == "RoadCurve" else 1.15))
    camera.location = target + offset * max(size, 1.5) * 1.5
    aim_at(camera, target)
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = size * (1.27 if sample in ("Engineer", "Mine") else 1.55)
    camera.data.clip_end = max(100, 12*size)
    scene.camera = camera
    unit = max(size, 2)
    for label, coord, color, power in [
        ("Key", (1, -2, 3), (1, .84, .65), 90 if dark else 185),
        ("Fill", (-2, 1, 2), (.48, .72, 1), 35 if dark else 120),
    ]:
        light = bpy.data.lights.new(scene.name + "_" + label, "AREA")
        light.size = unit * .85
        light.color = color
        light.energy = power * unit * unit
        obj = bpy.data.objects.new(light.name, light)
        obj.location = target + Vector(coord) * unit
        scene.collection.objects.link(obj)
        aim_at(obj, target)
    ground = bpy.data.meshes.new(scene.name + "_ReviewGround")
    width = size * 7
    ground.from_pydata([(target.x+x*width, target.y+y*width, min(-.03, low.z-.01))
                        for x, y in [(-1,-1), (1,-1), (1,1), (-1,1)]], [], [(0,1,2,3)])
    ground_object = bpy.data.objects.new(ground.name, ground)
    scene.collection.objects.link(ground_object)
    material = bpy.data.materials.get("Pilot_ReviewBackdrop") or bpy.data.materials.new("Pilot_ReviewBackdrop")
    material.use_nodes = True
    material.node_tree.nodes.get("Principled BSDF").inputs["Base Color"].default_value = (.077,.083,.101,1)
    ground.materials.append(material)
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 24
    scene.cycles.seed = 42
    scene.cycles.use_denoising = True
    scene.render.resolution_x = scene.render.resolution_y = 540
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = "AgX"


def author(render=False):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scratch = bpy.context.scene
    overrides = {}
    PREVIEW.mkdir(parents=True, exist_ok=True)
    for sample, files in SAMPLES.items():
        scene = bpy.data.scenes.new("Pilot_" + sample)
        scene.unit_settings.system = "METRIC"
        bpy.context.window.scene = scene
        objects = []
        for path in files:
            group = import_reference(path, scene)
            if sample == "Mine":
                location = {"Mine_Support": (0, 0, 0), "Mine_Lantern": (.56, .12, .05),
                            "Ground_Mine_2m": (0, 0, 0)}[path.rsplit("/", 1)[1]]
                for obj in group:
                    if obj.parent not in group:
                        obj.location += Vector(location)
            objects.extend(group)
        bpy.context.view_layer.update()
        setup_view(scene, sample, objects)
        if render:
            scene.render.filepath = str(PREVIEW / (sample + "_source.png"))
            bpy.ops.render.render(write_still=True)
        for obj in objects:
            if obj.type != "MESH":
                continue
            uv = obj.data.uv_layers[0].name
            for slot in obj.material_slots:
                base = slot.material
                name = base.name.split(".")[0]
                if name in MATERIALS:
                    key = (name, uv)
                    if key not in overrides:
                        overrides[key] = override(base, name, uv)
                    slot.link = "OBJECT"
                    slot.material = overrides[key]
        if render:
            scene.render.filepath = str(PREVIEW / (sample + "_pilot.png"))
            bpy.ops.render.render(write_still=True)
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action, do_unlink=True)
    bpy.data.scenes.remove(scratch)
    bpy.context.window.scene = bpy.data.scenes["Pilot_Workshop"]
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "Source/Shared_Texture_Pilot.blend"), compress=True)


if __name__ == "__main__":
    audit()
    shared_tiles()
    author(render="--render" in sys.argv)
