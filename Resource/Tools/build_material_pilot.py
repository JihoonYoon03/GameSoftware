"""Build shared map sources, textured glTF previews, and a mesh audit for GFX-01/03."""
from pathlib import Path
import hashlib
import io
import json
import math
import os
import random

from PIL import Image
from verify_resource_pack import accessor, read_glb

ROOT = Path(__file__).resolve().parents[1]
EXPORT = ROOT / "Export"
PILOT = EXPORT / "MaterialPilot"
PREVIEW = ROOT / "Preview/MaterialPilot"
AUDIT = ROOT / "Validation/material_pilot.json"
MANIFEST = ROOT / "Documentation/material_pilot.json"
ROAD_CONTRACT = ROOT / "Documentation/road_plan_modules.json"

ASSETS = {
    "habitat": "Buildings/Habitat_7p2m.glb",
    "curve": "RoadPlan/RoadPlan_Curve_2L_A30_R18.glb",
    "sidewalk": "RoadPlan/RoadPlan_WalkCurve_2L_A30_R18.glb",
    "paint": "RoadPlan/RoadPlan_CurveMarking_2L_A30_R18.glb",
    "ramp": "RoadPlan/RoadPlan_Ramp_2L_Rise8p4_L110.glb",
    "coupe": "Vehicles/Coupe.glb",
    "explorer": "Characters/Player_Explorer.glb",
    "mine_rock": "Environment/Nature_Rock_01.glb",
    "mine_support": "Modules/Mine_Support.glb",
}
MAPS = ("BaseColor", "Normal", "ORM")
EXISTING_SETS = ("Habitat_Concrete", "Habitat_Panels", "Road_Basalt", "Rock_Mine")
SHARED_PILOT_SETS = ("Pilot_PaintedAlloy", "Pilot_BrushedSteel", "Pilot_TechnicalWeave", "Pilot_CastPaving")
# Neutral color factors multiply the original glTF material tint in linear space.
GENERATED = {
    "SkinMicrograin": (463115, 0.63, 0.00, 0.98, 0.032, 0.10),
}
MATERIAL_MAPS = {
    "Road_Asphalt": "Road_Basalt", "Walk_Paving": "Pilot_CastPaving",
    "Road_Edge": "Pilot_CastPaving", "Vehicle_Teal": "Pilot_PaintedAlloy",
    "Steel": "Pilot_BrushedSteel", "Alloy_Light": "Pilot_BrushedSteel",
    "Frame_Graphite": "Pilot_PaintedAlloy", "Fastener_Titanium": "Pilot_BrushedSteel",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def srgb(value):
    return 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055


def pixel(value):
    return round(255 * min(1, max(0, value)))


def periodic_noise(size, grid, rng):
    """Smooth periodic noise sampled at pixel centers."""
    values = [rng.uniform(-1, 1) for _ in range(grid * grid)]
    coords = []
    for index in range(size):
        position = (index + 0.5) * grid / size
        left = int(position)
        fraction = position - left
        coords.append((left % grid, (left + 1) % grid, fraction**2 * (3 - 2 * fraction)))
    for bottom, top, vertical in coords:
        for left, right, horizontal in coords:
            a = values[bottom * grid + left] * (1 - horizontal) + values[bottom * grid + right] * horizontal
            b = values[top * grid + left] * (1 - horizontal) + values[top * grid + right] * horizontal
            yield a * (1 - vertical) + b * vertical


def build_maps():
    size = 512
    for name, (seed, roughness, metallic, color, contrast, relief) in GENERATED.items():
        rng = random.Random(seed)
        coarse = list(periodic_noise(size, 16, rng))
        fine = list(periodic_noise(size, 135, rng))
        height = [0.24 * a + 0.76 * b for a, b in zip(coarse, fine)]
        channels = {kind: bytearray() for kind in MAPS}
        for y in range(size):
            for x in range(size):
                i = y * size + x
                p = pixel(srgb(color - contrast * (0.5 + 0.5 * height[i])))
                channels["BaseColor"].extend((p, p, p))
                dx = relief * (height[y * size + (x + 1) % size] - height[y * size + (x - 1) % size])
                dy = relief * (height[((y + 1) % size) * size + x] - height[((y - 1) % size) * size + x])
                length = math.sqrt(1 + dx * dx + dy * dy)
                channels["Normal"].extend(pixel(0.5 + 0.5 * v / length) for v in (-dx, -dy, 1))
                channels["ORM"].extend((255, pixel(roughness + 0.08 * height[i]), pixel(metallic)))
        for kind, data in channels.items():
            image = Image.frombytes("RGB", (size, size), bytes(data))
            image.save(PILOT / "Textures" / f"{name}_{kind}.png")


def map_path(name, kind):
    directory = EXPORT / "Textures"
    if name in GENERATED:
        directory = PILOT / "Textures"
    elif name in SHARED_PILOT_SETS:
        directory = directory / "Pilot"
    return directory / f"{name}_{kind}.png"


def map_audit(name, kind):
    path = map_path(name, kind)
    image = Image.open(path).convert("RGB")
    side = image.width
    assert side == image.height and side & (side - 1) == 0
    levels = []
    while True:
        # Compare the wrap step with the full tile's adjacent steps at each mip.
        # Use linear-light filtering for color and normalize data normals after resizing.
        pixels = list(image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata())
        normal_error = []
        if kind == "Normal":
            normal_error = [abs(math.sqrt(sum((v / 127.5 - 1) ** 2 for v in p)) - 1) for p in pixels]
            assert min(p[2] for p in pixels) >= 128 and max(normal_error) < 0.035, path
        boundary = [abs(pixels[i][c] - pixels[i + side - 1][c]) for i in range(0, side * side, side) for c in range(3)]
        boundary += [abs(pixels[i][c] - pixels[(side - 1) * side + i][c]) for i in range(side) for c in range(3)]
        step = 11 if side > 128 else 1
        body = [abs(pixels[i][c] - pixels[i + 1][c]) for i in range(0, len(pixels) - 1, step) if (i + 1) % side for c in range(3)]
        body += [abs(pixels[i][c] - pixels[i + side][c]) for i in range(0, len(pixels) - side, step) for c in range(3)]
        edge_mean = sum(boundary) / max(1, len(boundary))
        body_mean = sum(body) / max(1, len(body))
        levels.append(dict(side=side, wrap_mean=round(edge_mean, 3), interior_mean=round(body_mean, 3),
                           normal_max_length_error=round(max(normal_error, default=0), 4)))
        if name in (*GENERATED, *SHARED_PILOT_SETS) and side > 2:
            assert edge_mean < max(2.0, body_mean * 2.5), (path, side, edge_mean, body_mean)
        if side == 1:
            break
        image = downsample(image, pixels, side, kind)
        side //= 2
    return dict(file=path.relative_to(ROOT).as_posix(), sha256=sha(path.read_bytes()), size=list(Image.open(path).size),
                color_space="sRGB" if kind == "BaseColor" else "linear",
                channels={"BaseColor":"RGB", "Normal":"XYZ tangent space +Y", "ORM":"R=AO, G=roughness, B=metallic"}[kind],
                mip_review=levels, rgba8_full_mip_bytes=sum(m["side"]**2 * 4 for m in levels))


def downsample(image, pixels, side, kind):
    if kind == "ORM":
        return image.resize((side // 2, side // 2), Image.Resampling.BOX)
    linear = [(v / 255 / 12.92 if v <= 10 else ((v / 255 + 0.055) / 1.055)**2.4) for v in range(256)]
    result = []
    for y in range(0, side, 2):
        for x in range(0, side, 2):
            i = y * side + x
            texels = [pixels[j] for j in (i, i + 1, i + side, i + side + 1)]
            if kind == "BaseColor":
                result.append(tuple(pixel(srgb(sum(linear[p[c]] for p in texels) / 4)) for c in range(3)))
            else:
                vector = [sum(p[c] / 127.5 - 1 for p in texels) for c in range(3)]
                length = math.sqrt(sum(v * v for v in vector))
                result.append(tuple(pixel(0.5 + 0.5 * v / length) for v in vector))
    mip = Image.new("RGB", (side // 2, side // 2))
    mip.putdata(result)
    return mip


def linked_set(name):
    if name.startswith("Cloth_"):
        return "Pilot_TechnicalWeave"
    if name.startswith("Skin_"):
        return "SkinMicrograin"
    return MATERIAL_MAPS.get(name)


def material_resources(doc, binary):
    # Externalize authored PNGs only after checking decoded pixels against the shared file.
    embedded_views = set()
    for image in doc.get("images", []):
        index = image.pop("bufferView")
        view = doc["bufferViews"][index]
        offset = view.get("byteOffset", 0)
        embedded = binary[offset:offset + view["byteLength"]]
        shared = EXPORT / "Textures" / (image["name"] + ".png")
        assert Image.open(io.BytesIO(embedded)).convert("RGBA").tobytes() == Image.open(shared).convert("RGBA").tobytes(), shared
        image["uri"] = "../Textures/" + shared.name
        embedded_views.add(index)
    samplers = doc.setdefault("samplers", [])
    sampler = len(samplers)
    samplers.append(dict(wrapS=10497, wrapT=10497, magFilter=9729, minFilter=9987))
    images = doc.setdefault("images", [])
    textures = doc.setdefault("textures", [])
    texture_by_name = {}

    def bind(name, kind):
        key = (name, kind)
        if key not in texture_by_name:
            uri = Path(os.path.relpath(map_path(name, kind), PILOT)).as_posix()
            images.append(dict(name=f"{name}_{kind}", uri=uri))
            texture_by_name[key] = len(textures)
            textures.append(dict(source=len(images) - 1, sampler=sampler))
        return dict(index=texture_by_name[key], texCoord=0)

    for material in doc["materials"]:
        name = material["name"]
        surface = linked_set(name)
        pbr = material.setdefault("pbrMetallicRoughness", {})
        if surface:
            assert "baseColorTexture" not in pbr and material.get("alphaMode", "OPAQUE") == "OPAQUE", name
            if surface in EXISTING_SETS:
                pbr["baseColorFactor"] = [1, 1, 1, 1]
            pbr["baseColorTexture"] = bind(surface, "BaseColor")
            pbr["metallicRoughnessTexture"] = bind(surface, "ORM")
            pbr["metallicFactor"] = pbr["roughnessFactor"] = 1
            material["occlusionTexture"] = bind(surface, "ORM")
            material["normalTexture"] = {**bind(surface, "Normal"), "scale": 0.54 if surface == "Rock_Mine" else 0.32}
        elif name in EXISTING_SETS:
            surface = name
            if "occlusionTexture" not in material:
                material["occlusionTexture"] = dict(pbr["metallicRoughnessTexture"])
        material.setdefault("extras", {}).update(material_id="erma.material." + name,
                                                texture_set_id=f"erma.surface.{surface}" if surface else None)
    return embedded_views


def compact_buffer(doc, binary, exclude):
    views = doc["bufferViews"]
    output = bytearray()
    indices = {}
    for index, view in enumerate(views):
        if index in exclude:
            continue
        start = view.get("byteOffset", 0)
        output.extend(b"\0" * (-len(output) % 4))
        view["byteOffset"] = len(output)
        output.extend(binary[start:start + view["byteLength"]])
        indices[index] = len(indices)
    doc["bufferViews"] = [v for i, v in enumerate(views) if i in indices]

    def remap(value):
        if isinstance(value, dict):
            if "bufferView" in value:
                value["bufferView"] = indices[value["bufferView"]]
            for child in value.values():
                remap(child)
        elif isinstance(value, list):
            for child in value:
                remap(child)

    remap(doc)
    return bytes(output)


def subtract(a, b):
    return tuple(x - y for x, y in zip(a, b))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def audit_asset(doc, binary, original, original_bin, sets):
    primitives = []
    mesh_totals = {}
    assert all(doc.get(k) == original.get(k) for k in ("scenes", "nodes", "skins", "animations", "meshes"))
    for index in range(len(doc["accessors"])):
        assert accessor(doc, binary, index) == accessor(original, original_bin, index), index
    for mesh in doc["meshes"]:
        part_triangles = 0
        for primitive in mesh["primitives"]:
            material = doc["materials"][primitive["material"]]
            set_id = material["extras"]["texture_set_id"]
            image_width = sets[set_id]["BaseColor"]["size"][0] if set_id else None
            attrs = primitive["attributes"]
            pos, normal, uv, tangent = (accessor(doc, binary, attrs[key]) for key in ("POSITION", "NORMAL", "TEXCOORD_0", "TANGENT"))
            ids = [row[0] for row in accessor(doc, binary, primitive["indices"])]
            assert len(ids) % 3 == 0 and len(pos) == len(normal) == len(uv) == len(tangent)
            assert all(math.isfinite(v) for values in (pos, normal, uv, tangent) for row in values for v in row)
            assert all(0 <= i < len(pos) for i in ids)
            invalid = sum(abs(dot(t[:3], t[:3]) - 1) > 0.005 or abs(dot(n, t[:3])) > 0.005 or abs(abs(t[3]) - 1) > 0.001
                          for n, t in zip(normal, tangent))
            degenerate = 0
            density = []
            for i in range(0, len(ids), 3):
                a, b, c = ids[i:i+3]
                u, v = subtract(uv[b], uv[a]), subtract(uv[c], uv[a])
                uv_area = abs(u[0] * v[1] - u[1] * v[0])
                triangle = cross(subtract(pos[b], pos[a]), subtract(pos[c], pos[a]))
                area = math.sqrt(dot(triangle, triangle))
                if uv_area < 1e-12 or area < 1e-12:
                    degenerate += 1
                elif image_width:
                    density.append(image_width * math.sqrt(uv_area / area))
            if set_id:
                assert invalid == degenerate == 0, (mesh["name"], material["name"], invalid, degenerate)
            part_triangles += len(ids) // 3
            sorted_density = sorted(density)
            primitives.append(dict(mesh=mesh["name"], material_id=material["extras"]["material_id"], vertices=len(pos),
                                   triangles=len(ids)//3, degenerate_uv_or_geometry=degenerate, invalid_tangents=invalid,
                                   mirrored_vertices=sum(t[3] < 0 for t in tangent),
                                   density_texels_per_local_m_median=round(sorted_density[len(density)//2], 1) if density else None))
        assert 0 < part_triangles <= 200, (mesh["name"], part_triangles)
        mesh_totals[mesh["name"]] = part_triangles
    return primitives, mesh_totals


def main():
    for folder in (PILOT / "Textures", PREVIEW):
        folder.mkdir(parents=True, exist_ok=True)
    build_maps()
    sets = {f"erma.surface.{name}": {kind: map_audit(name, kind) for kind in MAPS}
            for name in (*EXISTING_SETS, *SHARED_PILOT_SETS, *GENERATED)}
    results = {}
    manifest_assets = {}
    materials = {}
    for key, file in ASSETS.items():
        path = EXPORT / file
        original, source = read_glb(path)
        doc, binary = read_glb(path)
        embedded = material_resources(doc, binary)
        output = compact_buffer(doc, binary, embedded)
        primitives, parts = audit_asset(doc, output, original, source, sets)
        bindings = [m["extras"]["material_id"] for m in doc["materials"]]
        for m in doc["materials"]:
            name = m["extras"]["material_id"]
            mode = m.get("alphaMode", "OPAQUE")
            record = dict(texture_set_id=m["extras"]["texture_set_id"], alpha=mode, cutoff=m.get("alphaCutoff"),
                          double_sided=m.get("doubleSided", False),
                          shadow="requires explicit blend policy" if mode == "BLEND" else "opaque caster or asset override")
            assert name not in materials or materials[name] == record, name
            materials[name] = record
        doc["buffers"][0].update(uri=f"{key}.bin", byteLength=len(output))
        (PILOT / f"{key}.bin").write_bytes(output)
        save_json(PILOT / f"{key}.gltf", doc)
        opaque = sum(m["alpha"] == "OPAQUE" for m in [materials[k] for k in bindings])
        goal = 3 if key == "explorer" else 2
        set_ids = sorted({materials[m]["texture_set_id"] for m in bindings} - {None})
        manifest_assets[key] = dict(source=path.relative_to(ROOT).as_posix(), source_sha256=sha(path.read_bytes()),
            gltf=f"Export/MaterialPilot/{key}.gltf", gltf_sha256=sha((PILOT/f"{key}.gltf").read_bytes()),
            geometry=f"Export/MaterialPilot/{key}.bin", geometry_sha256=sha(output), material_ids=bindings,
            texture_set_ids=set_ids, embedded_images_removed=len(embedded), material_slots=len(bindings),
            opaque_slots=opaque, opaque_slot_goal=goal, retained_opaque_slots_above_goal=max(0, opaque-goal),
            nodes_with_mesh=[dict(part_id=n.get("extras",{}).get("asset_id",n.get("name")), node_index=i, mesh_index=n["mesh"],
                                  skin_index=n.get("skin")) for i,n in enumerate(doc["nodes"]) if "mesh" in n],
            skeleton_joint_counts=[len(s["joints"]) for s in doc.get("skins",[])],
            animations=[a["name"] for a in doc.get("animations",[])])
        results[key] = dict(geometry_unchanged=True, skin_and_animations_unchanged=True, original_parts=parts,
                           primitives=primitives, texture_normal_issues=sum(p["invalid_tangents"] for p in primitives))
        print(key, len(parts), sum(parts.values()), len(embedded), flush=True)
    assert len({m["sha256"] for s in sets.values() for m in s.values()}) <= len(sets) * 3
    save_json(MANIFEST, dict(schema="erma.material.pilot", version=1, compatibility="authoring glTF 2.0 with external PNGs",
        builder="Tools/build_material_pilot.py", builder_sha256=sha(Path(__file__).read_bytes()), authoring_units="meter",
        coordinate_system="GLTF +Y up, source node transforms retained", uv="TEXCOORD_0, upper left origin, repeat",
        sampling="linear mag, trilinear min, repeat; review mips computed offline, PNG output is base level",
        tangent="original glTF tangent.w handedness; bitangent = cross(N,T) * w", maps_data_license="Project-owned source and procedural authoring",
        status="pilot resources produced; runtime integration pending", road_contract_sha256=sha(ROAD_CONTRACT.read_bytes()),
        texture_sets=sets, material_templates=materials, assets=manifest_assets,
        all_unique_maps_rgba8_full_mip_bytes=sum(m["rgba8_full_mip_bytes"] for m in
            {m["sha256"]:m for s in sets.values() for m in s.values()}.values())))
    save_json(AUDIT, dict(status="PASS", scope="Pilot bindings and preserved source geometry, not native game rendering",
                          metrics=results))


if __name__ == "__main__":
    main()
