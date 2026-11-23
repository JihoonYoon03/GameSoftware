"""Reopen the delivered Blender pilot and validate geometry and material links."""
from collections import Counter
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from texture_pilot import MATERIALS, ROOT, SAMPLES, audit, digest, write_json

inputs, _ = audit()
blend = ROOT / "Source/Shared_Texture_Pilot.blend"
bpy.ops.wm.open_mainfile(filepath=str(blend))
report = {"blender_version": bpy.app.version_string, "blend_sha256": digest(blend), "scenes": {}}
assert bpy.context.scene.name == "Pilot_Workshop"
assert not bpy.data.actions, "existing animation bank was copied into the material preview"

for sample, sources in SAMPLES.items():
    scene = bpy.data.scenes["Pilot_" + sample]
    bpy.context.window.scene = scene
    parts = [obj for obj in scene.objects if obj.type == "MESH" and obj.get("asset_id")]
    expected = [part for source in sources for part in inputs[source]["parts"]]
    assert Counter(obj["asset_id"] for obj in parts) == Counter(part["part_id"] for part in expected)
    assert Counter((obj["asset_id"], len(obj.data.loop_triangles)) for obj in parts) == Counter(
        (part["part_id"], part["triangles"]) for part in expected)
    rig = [obj for obj in scene.objects if obj.type == "ARMATURE"]
    if sample == "Engineer":
        assert len(rig) == 1 and len(rig[0].data.bones) == 23
    texture_slots = 0
    for obj in parts:
        assert obj.data.uv_layers
        assert all(abs(component) > 1e-6 for component in obj.matrix_world.to_scale())
        for slot in obj.material_slots:
            material = slot.material
            if material.get("texture_set"):
                texture_slots += 1
                origin = material.name.removeprefix("Pilot_")
                assert origin in MATERIALS
                nodes = material.node_tree.nodes
                images = {n.label: n.image for n in nodes if n.type == "TEX_IMAGE"}
                assert set(images) == {"BaseColor", "Normal", "ORM"}
                for label, image in images.items():
                    assert image.packed_file and image.colorspace_settings.name == (
                        "sRGB" if label == "BaseColor" else "Non-Color")
                assert any(n.type == "NORMAL_MAP" and n.uv_map == obj.data.uv_layers[0].name
                           for n in nodes)
    assert texture_slots > 0
    report["scenes"][sample] = {"parts": len(parts), "preview_override_slots": texture_slots,
                                "rig_bones": [len(obj.data.bones) for obj in rig]}
report["all_checks_passed"] = True
write_json(ROOT / "Validation/texture_pilot_blender.json", report)
print("BLENDER_TEXTURE_PILOT_REOPENED", len(report["scenes"]), flush=True)
