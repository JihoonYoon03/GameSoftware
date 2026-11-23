"""Write the pilot handoff manifest and verify textures and source reuse."""
from collections import Counter
import json
import math

from PIL import Image, ImageDraw, ImageFont

from texture_pilot import (MAPS, NEW, PREVIEW, ROOT, SAMPLES, audit, digest, glb_path,
                           image_path, write_json)


def texture_records(materials):
    records = {}
    for name in sorted({row["texture_set"] for row in materials.values()} - {None}):
        maps = {}
        for channel in MAPS:
            path = image_path(name, channel)
            with Image.open(path) as image:
                width, height = image.size
                size = 1024 if name in NEW else 512
                assert image.size == (size, size) and image.mode in {"RGB", "RGBA"}
                rgb = image.convert("RGB")
                red, green, blue = rgb.getextrema()
                if channel == "Normal":
                    samples = rgb.resize((96, 96), Image.Resampling.NEAREST)
                    values = samples.tobytes()
                    max_error = max(abs(math.sqrt(sum((2*v/255-1)**2 for v in p)) - 1)
                                    for p in zip(values[::3], values[1::3], values[2::3]))
                    if name in NEW:
                        assert max_error < .015 and blue[0] > 240
                if channel == "ORM" and name in NEW:
                    assert red == (255, 255) and green[0] < green[1] and green[0] > 0
                if name in NEW:
                    # Wrap is manufactured periodically. Bound the seam step
                    # against typical within-tile neighbour changes.
                    pixels = rgb.load()
                    for axis in (0, 1):
                        probes = range(1, size, 31)
                        if axis == 0:
                            edge = sum(abs(a-b) for y in range(size)
                                       for a, b in zip(pixels[0, y], pixels[size-1, y]))
                            local = sum(abs(a-b) for y in range(size) for x in probes
                                        for a, b in zip(pixels[x-1, y], pixels[x, y]))
                        else:
                            edge = sum(abs(a-b) for x in range(size)
                                       for a, b in zip(pixels[x, 0], pixels[x, size-1]))
                            local = sum(abs(a-b) for x in range(size) for y in probes
                                        for a, b in zip(pixels[x, y-1], pixels[x, y]))
                        assert edge <= local / len(probes) * 2.2 + size * 2.4, (name, channel, axis, edge)
                levels = width.bit_length()
                rgba8_bytes = sum(4 * max(1, width >> k) * max(1, height >> k) for k in range(levels))
            maps[channel] = {
                "image_id": f"{name}.{channel}", "path": path.relative_to(ROOT).as_posix(),
                "sha256": digest(path), "source_png_bytes": path.stat().st_size,
                "format": "PNG_lossless", "width": width, "height": height,
                "colour_space": "sRGB" if channel == "BaseColor" else "linear",
                "channels": {"BaseColor": "RGB=albedo", "Normal": "RGB=tangent_xyz, +Y",
                             "ORM": "R=AO,G=roughness,B=metallic"}[channel],
                "full_mip_level_count": levels, "estimated_full_mips_rgba8_bytes": rgba8_bytes,
            }
        records[name] = {"layout": "repeat_tile", "sampler": "repeat_trilinear_proposed",
                         "atlas_padding": "not_applicable_repeat_tile", "maps": maps,
                         "provenance": "in-house procedural source" if name in NEW else "existing project source",
                         "editable_source": "Tools/build_texture_pilot.py" if name in NEW else "Tools/build_resource_pack.py"}
    return records


def compose():
    scale = 300
    left = 11
    sheet = Image.new("RGB", (5 * (scale + 2 * left), 2 * (scale + 24) + 55), "#171e29")
    painter = ImageDraw.Draw(sheet)
    font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 18)
    title = ImageFont.truetype("C:/Windows/Fonts/malgunbd.ttf", 24)
    labels = ("정비소", "곡선 도로·보도", "쿠페", "리아의 작업복", "광산 지지대·등")
    painter.text((left, 10), "공유 텍스처 시범 세트", font=title, fill="#e2eeef")
    for i, (category, label) in enumerate(zip(SAMPLES, labels)):
        x = i * (scale + 2 * left) + left
        for j, (suffix, description) in enumerate((("source", "원본"), ("pilot", "시범"))):
            y = j * (scale + 24) + 54
            with Image.open(PREVIEW / (category + "_" + suffix + ".png")) as image:
                assert image.size == (540, 540)
                sheet.paste(image.convert("RGB").resize((scale, scale), Image.Resampling.LANCZOS), (x, y + 22))
            painter.text((x, y - 3), f"{label}  /  {description}", font=font, fill="#d9e8ed")
    sheet.save(PREVIEW / "comparison.jpg", quality=93)


def main():
    inputs, materials = audit()
    textures = texture_records(materials)
    blend = ROOT / "Source/Shared_Texture_Pilot.blend"
    mesh_log = ROOT / "Validation/texture_pilot_blender.json"
    check = json.loads(mesh_log.read_text(encoding="utf-8"))
    assert check["all_checks_passed"] and check["blend_sha256"] == digest(blend)
    map_references = [image for row in textures.values() for image in row["maps"].values()]
    maps = {(image["sha256"], image["colour_space"]): image for image in map_references}
    expected = {name: sorted(mat for mat in materials if materials[mat]["texture_set"] == name)
                for name in textures}
    opaque_targets = {"Workshop": 2, "RoadCurve": 2, "Coupe": 2, "Engineer": 3, "Mine": 2}
    costs = {}
    for name, sources in SAMPLES.items():
        slot_counts = Counter(slot for source in sources for slot in inputs[source]["material_ids"])
        opaque = sorted(slot for slot in slot_counts if materials[slot]["alpha_mode"] == "OPAQUE"
                        and not any(materials[slot]["emissive_factor"]))
        emissive = sorted(slot for slot in slot_counts if any(materials[slot]["emissive_factor"]))
        blend_slots = sorted(slot for slot in slot_counts if materials[slot]["alpha_mode"] != "OPAQUE")
        sets = {materials[slot]["texture_set"] for slot in slot_counts} - {None}
        costs[name] = {
            "opaque_distinct_material_ids": opaque, "opaque_slot_goal": opaque_targets[name],
            "over_review_goal": len(opaque) > opaque_targets[name],
            "emissive_material_ids": emissive, "non_opaque_material_ids": blend_slots,
            "shared_texture_sets_used": sorted(sets),
            "draw_count": "requires game integration measurement",
        }
    manifest = {
        "schema": "erma_shared_texture_pilot_v1", "date": "2026-11-23",
        "request_ids": ["GFX-01", "GFX-03"], "state": "Blender authoring pilot",
        "authoring": {"blend": blend.relative_to(ROOT).as_posix(), "sha256": digest(blend),
                      "tool": check["blender_version"], "built_by": "Tools/build_texture_pilot.py"},
        "coordinates": {"meters": True, "authoring_to_gltf": "(x,z,-y)", "uv": "TEXCOORD_0",
                        "gltf_image_origin": "upper left; Blender importer flips v internally once",
                        "tangent_basis": "TANGENT.xyz, B=cross(N,T)*TANGENT.w; tangent-space normal +Y",
                        "texture_uv_multiplier": "positive uniform scale; apply at sampling"},
        "material_families": {"skin": "existing tint and roughness constants", "glass": "separate existing alpha state",
                              "emissive": "existing material constant and strength"},
        "texture_sets": textures, "texture_dependents": expected,
        "mip_handoff": {"payload": "lossless base levels",
                        "BaseColor": "downsample in linear light, encode in sRGB",
                        "Normal": "average signed vectors then renormalize",
                        "ORM": "independent linear channels",
                        "estimated_bytes_include": "full mip chain for every unique uncompressed RGBA8 image"},
        "fallback": {"BaseColor": [1, 1, 1], "Normal": [.5, .5, 1], "ORM": [1, 1, 0], "Emissive": [0, 0, 0]},
        "materials": materials, "scenes": {k: list(v) for k, v in SAMPLES.items()}, "prefabs": inputs,
        "costs": {"per_scene": costs, "map_references": len(map_references), "unique_image_count": len(maps),
                  "unique_png_bytes": sum(m["source_png_bytes"] for m in maps.values()),
                  "estimated_all_unique_full_mips_rgba8_bytes":
                      sum(m["estimated_full_mips_rgba8_bytes"] for m in maps.values())},
        "road_paint": {"reference": "Documentation/road_plan_modules.json",
                       "offset": "original GLB coordinates, no baked placement bias",
                       "geometry_roles": "separate drive surface, marking, paving and curb",
                       "collision_connectivity": "original RoadPlan port and gameplay contracts"},
        "runtime_integration": "pending; no versioned runtime texture payload emitted",
    }
    assert all(glb_path(name).is_file() for name in inputs)
    write_json(ROOT / "Documentation/shared_texture_pilot.json", manifest)
    compose()
    print("TEXTURE_PILOT_VERIFIED", len(inputs), "sources", len(textures), "texture sets", len(maps), "images")


if __name__ == "__main__":
    main()
