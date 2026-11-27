"""Check exported GLBs and PNGs independently of the Blender authoring session."""
import hashlib
import json
import math
import struct
from pathlib import Path

from verify_resource_pack import read_glb

ROOT=Path(__file__).resolve().parents[1]
STAGES=("LOD0","LOD1","LOD2","ShadowProxy")


def load(name):
    return json.loads((ROOT/"Documentation"/name).read_text("utf-8"))


def checked_file(entry):
    path=ROOT/entry.get("file",entry.get("path"))
    assert hashlib.sha256(path.read_bytes()).hexdigest()==entry["sha256"],path
    return path


def check_glb(row):
    path=checked_file(row)
    doc,_=read_glb(path)
    assert all(not set(n)&{"matrix","scale","translation","rotation"} for n in doc["nodes"]),path
    total=0
    lo,hi=[math.inf]*3,[-math.inf]*3
    for mesh in doc["meshes"]:
        for part in mesh["primitives"]:
            fields=part["attributes"]
            assert {"POSITION","TEXCOORD_0","NORMAL","TANGENT"} <= set(fields), path
            position=doc["accessors"][fields["POSITION"]]
            lo=[min(a,b) for a,b in zip(lo,position["min"])]
            hi=[max(a,b) for a,b in zip(hi,position["max"])]
            triangle_count=doc["accessors"][part["indices"]]["count"]/3
            assert triangle_count.is_integer() and part.get("mode",4)==4
            total+=int(triangle_count)
    expected=list(zip(*row["bounds_m"]))
    for axis, (a,b) in enumerate([(lo[0],hi[0]),(-hi[2],-lo[2]),(lo[1],hi[1])]):
        assert max(abs(a-expected[axis][0]),abs(b-expected[axis][1]))<2e-5,(path,axis,a,b,expected)
    assert total==row["triangles"]<=row["budget"] and math.isfinite(total)
    if not path.stem.endswith("_ShadowProxy"):
        assert doc.get("images"),path
    for image in doc.get("images",[]):
        target=(path.parent/image["uri"]).resolve()
        assert target.is_relative_to(ROOT/"Export/Textures/ColonyV4") and target.is_file(),target
    return total


def main():
    data=load("colony_v4_assets.json")
    textures=load("colony_v4_textures.json")["textures"]
    buildings,props=data["buildings"],data["props"]
    assert len(buildings)==38 and len(props)==26 and len(data["replacements"])==10
    old=load("measured_buildings.json")["prefabs"]
    counts={}
    for name,asset in (buildings|props).items():
        stages=asset["levels"]
        assert tuple(stages)==STAGES
        counts[name]=[check_glb(stages[k]) for k in STAGES]
        assert counts[name][0]>=counts[name][1]>=counts[name][2]
        if name in old:
            assert asset["entrances"]==old[name]["entrances"]
            d,w,h=old[name]["structure_dimensions_m"]
            assert asset["dimensions_wdh_m"]==[w,d,h]
        if name in buildings and name not in old:
            w,d,h=asset["dimensions_wdh_m"]
            for row in stages.values():
                lo,hi=row["bounds_m"]
                assert math.dist((hi[0]-lo[0],hi[1]-lo[1],hi[2]),(d,w,h))<.025
    assert len(textures)==33
    for name,texture in textures.items():
        path=checked_file(texture)
        raw=path.read_bytes()
        assert raw[:8]==b"\x89PNG\r\n\x1a\n"
        w,h,depth,kind=struct.unpack(">IIBB",raw[16:26])
        assert [w,h]==texture["size"] and depth==8 and kind in (2,6),name
        assert len(raw)==texture["bytes"]<100_000_000 and w*h*4<100_000_000
    print(json.dumps(dict(status="PASS", buildings=len(buildings), props=len(props),
                     glbs=sum(len(v) for v in counts.values()),textures=len(textures),
                     max_file_bytes=max(row["bytes"] for row in textures.values())),indent=2))


if __name__=="__main__":
    main()
