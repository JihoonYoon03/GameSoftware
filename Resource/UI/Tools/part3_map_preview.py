"""Project existing map artwork for standalone UI previews and audit texture budgets."""
import base64
import hashlib
from io import BytesIO
import math

from PIL import Image

TEXTURES = {
    "Planet_Erma_Albedo": (4096,2048), "Planet_Erma_Normal": (4096,2048), "Planet_Erma_Cloud": (4096,2048),
    "Planet_Seira_Albedo": (2048,1024), "Planet_Seira_Normal": (2048,1024),
    "Planet_Orren_Albedo": (2048,1024), "Planet_Orren_Normal": (2048,1024), "Planet_Orren_Ring": (1024,256),
    "Space_Starfield_Nebula": (2048,1024),
}


def collect(ui_root):
    directory=ui_root.parent/"Export/Textures/ColonyV4"
    files={}
    info={}
    for name, expected in TEXTURES.items():
        file=directory/(name+".png")
        with Image.open(file) as image:
            image.verify()
        with Image.open(file) as image:
            assert image.size==expected,(file,image.size,expected)
        stored=file.stat().st_size
        rgba8=expected[0]*expected[1]*4
        assert stored<100_000_000 and rgba8<100_000_000,file
        color_space="linear" if name.endswith("_Normal") else "sRGB color, linear opacity" if name.endswith(("_Cloud","_Ring")) else "sRGB"
        info[name]=dict(path=file.relative_to(ui_root.parent).as_posix(),dimensions=list(expected),
                        file_bytes=stored,rgba8_bytes=rgba8,sha256=hashlib.sha256(file.read_bytes()).hexdigest(),
                        color_space=color_space)
        files[name]=file
    return files,info


def data_url(image,fmt):
    output=BytesIO()
    image.save(output,fmt,**({"quality":84,"optimize":True} if fmt=="JPEG" else {"optimize":True}))
    return f"data:image/{fmt.lower()};base64,"+base64.b64encode(output.getvalue()).decode("ascii")


def globe(path, side, rotation):
    with Image.open(path) as original:
        albedo=original.convert("RGB").resize((1024,512),Image.Resampling.LANCZOS)
    pixels=albedo.load()
    result=Image.new("RGBA",(side,side))
    out=result.load()
    for y in range(side):
        ny=1-2*(y+.5)/side
        for x in range(side):
            nx=2*(x+.5)/side-1
            radial=nx*nx+ny*ny
            if radial>=1:
                continue
            nz=math.sqrt(1-radial)
            u=(math.atan2(nx,nz)/math.tau+.5+rotation)%1
            v=.5-math.asin(ny)/math.pi
            factor=.44+.56*max(0,min(1,.78*nz-.37*nx+.30*ny))
            rgb=pixels[int(u*1024),min(511,int(v*512))]
            out[x,y]=(*(round(channel*factor) for channel in rgb),round(min(1,(1-math.sqrt(radial))*side*.5)*255))
    return data_url(result,"PNG")


def make_preview(ui_root):
    files,info=collect(ui_root)
    with Image.open(files["Space_Starfield_Nebula"]) as source:
        nebula=data_url(source.convert("RGB").resize((650,326),Image.Resampling.LANCZOS),"JPEG")
    return {"nebula":nebula,"erma":globe(files["Planet_Erma_Albedo"],288,.12),
            **{name:globe(files[f"Planet_{name}_Albedo"],64,.18) for name in ("Erma","Seira","Orren")}},info
