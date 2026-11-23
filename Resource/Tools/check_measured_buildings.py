"""Validate the measured prefab GLBs and assemble the quarter-view catalogue."""
import json
import math

from PIL import Image, ImageDraw, ImageFont

from texture_pilot import ROOT, write_json, digest
from verify_resource_pack import read_glb, accessor


def verify_building(prefab):
    path = ROOT / prefab["asset"]
    doc, binary = read_glb(path)
    assert not doc.get("skins") and not doc.get("animations")
    expected = sorted((row["part_id"], tuple(round(v,5) for v in row["translation_m"]))
                      for row in prefab["instances"])
    actual = []
    seen = {}
    for node in doc["nodes"]:
        if "mesh" not in node:
            continue
        pid = node["extras"]["asset_id"]
        assert "scale" not in node and "rotation" not in node and "matrix" not in node
        x,y,z = node.get("translation",[0,0,0])
        actual.append((pid, tuple(round(v,5) for v in (x,-z,y))))
        mesh = doc["meshes"][node["mesh"]]
        if pid in seen:
            assert seen[pid] == node["mesh"], (pid, "repeated floor duplicated its mesh")
            continue
        seen[pid] = node["mesh"]
        triangles = 0
        for primitive in mesh["primitives"]:
            attr = primitive["attributes"]
            assert {"POSITION","NORMAL","TEXCOORD_0","TANGENT"} <= set(attr)
            indices = accessor(doc, binary, primitive["indices"])
            uv = accessor(doc,binary,attr["TEXCOORD_0"])
            for a,b,c in zip(indices[::3],indices[1::3],indices[2::3]):
                a,b,c = uv[a[0]],uv[b[0]],uv[c[0]]
                assert abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])) > 1e-9
            tangents = accessor(doc,binary,attr["TANGENT"])
            assert all(all(math.isfinite(x) for x in t) and abs(abs(t[3])-1) < .001 for t in tangents)
            triangles += len(indices)//3
        part = prefab["parts"][pid]
        assert triangles == part["triangles"] <= part["triangle_budget"] <= 200
    assert actual and sorted(actual) == expected and set(seen) == set(prefab["parts"])
    for entry in prefab["entrances"]:
        assert entry["opening_width_m"] >= 1.45 - 1e-6 and entry["opening_height_m"] > 1.8
        (x0,y0),(x1,y1) = entry["keep_clear_xy_m"]
        for (a,b,_),(c,d,_) in prefab["collision"]["ground_wall_boxes_xyz_m"]:
            assert not (min(x1,c)>max(x0,a)+.0001 and min(y1,d)>max(y0,b)+.0001)
        assert prefab["floor_surface_z_m"] / entry["ramp_run_m"] <= .05
        assert entry["outdoor_connection_xyz_m"][2] == prefab["ground_contact_z_m"] == 0
    return {"sha256":digest(path),"parts":len(seen),"nodes":len(actual),
            "max_triangles":max(row["triangles"] for row in prefab["parts"].values())}


def catalogue(prefabs):
    folder = ROOT / "Preview/MeasuredBuildings"
    tile, stride = 235, 264
    sheet = Image.new("RGB", (stride*5, stride*3+46), "#182230")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 14)
    heading = ImageFont.truetype("C:/Windows/Fonts/malgunbd.ttf", 23)
    labels = ("시내 주거", "시내 정비소", "시내 의료소", "소도시 물류센터", "소도시 교통관",
              "소도시 시장", "대도시 복합 주거", "대도시 행정·상업", "수도 관문 행정", "수도 공공 포럼",
              "개척지 작업장", "개척지 운송 시설", "개척지 폐중계소", "광산 출입 지원동")
    draw.text((14,8), "실측 건물 14종  /  45도 검토", font=heading, fill="#eef6f7")
    for index, ((key, info), label) in enumerate(zip(prefabs.items(), labels)):
        x,y = index%5*stride+14,index//5*stride+44
        with Image.open(folder/(key+".png")) as img:
            sheet.paste(img.convert("RGB").resize((tile,tile),Image.Resampling.LANCZOS),(x,y))
        draw.text((x,y+tile+3), f"{label}  ·  {len(info['story_heights_m'])}층", font=font, fill="#d0e5e9")
    sheet.save(folder/"catalogue.jpg", quality=93)


if __name__ == "__main__":
    manifest = json.loads((ROOT/"Documentation/measured_buildings.json").read_text(encoding="utf-8"))
    prefabs = manifest["prefabs"]
    results = {key: verify_building(row) for key, row in prefabs.items()}
    assert len(results) == 14
    catalogue(prefabs)
    write_json(ROOT/"Validation/measured_buildings.json", {"buildings":results,"all_checks_passed":True})
    print("MEASURED_BUILDINGS_VERIFIED", len(results), sum(r["parts"] for r in results.values()))
