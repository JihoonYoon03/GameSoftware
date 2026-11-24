"""Create the original repeatable PBR maps used by the drawing-based characters."""
import struct
import zlib

import numpy as np


SIZE = 1024
SURFACES = ("Fabric", "Shell", "Skin")


def write_png(path, pixels):
    raster=np.rint(np.clip(pixels,0,1)*255).astype(np.uint8)
    height,width,channels=raster.shape
    scanlines=b"".join(b"\0"+row.tobytes() for row in raster)
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">2I5B",width,height,8,{3:2,4:6}[channels],0,0,0))
                     +chunk(b"IDAT",zlib.compress(scanlines,6))+chunk(b"IEND",b""))


def maps(x,y,rng,surface):
    def signal(a,b):
        return np.sin(a*x+b*y+rng.uniform(0,2*np.pi))

    broad=(signal(5,1)+signal(1,-8)+signal(12,5)+signal(3,-4))*.25
    grain=(signal(173,11)+signal(12,201)+signal(137,-141)+signal(267,175))*.25
    mid=(signal(43,31)+signal(27,-39)+signal(36,47)+signal(62,44))*.25
    if surface=="Fabric":
        warp=np.cos(x*116)*(.72+.28*np.cos(y*58))
        weft=np.cos(y*116)*(.71-.29*np.cos(x*58))
        diagonal=signal(58,-58)
        height=warp*.032+weft*.032+diagonal*.016+mid*.010
        base=.86+height*1.47+broad*.037+grain*.021
        ao=.94+height*.43
        rough=.79+height*.31+mid*.033
        metal=np.zeros_like(height)
    elif surface=="Shell":
        # Cast finish with fine paint texture and small, irregular scuffs.
        scuff=np.maximum(0,signal(17,29)*.56+mid*.53-.52)**2
        groove=np.maximum(0,signal(101,6)*.66+signal(199,15)*.34-.84)**2
        height=grain*.015+mid*.020-scuff*.006-groove*.023
        base=.91+broad*.058+grain*.014-scuff*.36-groove*.22
        ao=.98-groove*.20
        rough=.49+broad*.085+scuff*.40
        metal=.18+scuff*.50+grain*.018
    else:
        pores=(np.maximum(grain,.25)-.25)**2
        height=mid*.004-pores*.020
        base=.94+broad*.032+mid*.016-pores*.12
        ao=.99-pores*.12
        rough=.63+broad*.06+mid*.03
        metal=np.zeros_like(height)
    # PNG row zero corresponds to the top of the image, opposite the v axis.
    dx=(np.roll(height,-1,axis=1)-np.roll(height,1,axis=1))*2
    dy=(np.roll(height,-1,axis=0)-np.roll(height,1,axis=0))*2
    length=np.sqrt(1+dx*dx+dy*dy)
    normal=np.stack((-dx/length,dy/length,1/length),axis=2)*.5+.5
    return {"BaseColor":np.repeat(base[...,None],3,axis=2),"Normal":normal,
            "ORM":np.stack((ao,rough,metal),axis=2)}


def hair_atlas():
    """Seven strand clumps and an opaque radial tile for the nearby scalp surface."""
    rng=np.random.default_rng(138)
    atlas=np.empty((SIZE,SIZE,4),dtype=np.float32)
    atlas[:,:,:3]=(.165,.097,.066)
    atlas[:,:,3]=0
    width,height=SIZE//4,SIZE//2
    pad=10
    u=np.linspace(0,1,width-2*pad,dtype=np.float32)[None,:]
    v=np.linspace(0,1,height-2*pad,dtype=np.float32)[:,None]
    for index in range(7):
        coverage=np.zeros((v.size,u.size),dtype=np.float32)
        glow=np.zeros_like(coverage)
        curve=((index%4)-1.5)*.078
        for _ in range(109):
            root=rng.uniform(.095,.905)
            length=rng.uniform(.53,1.055)
            drift=rng.uniform(-.13,.13)
            rip=rng.uniform(.006,.027)
            fiber=rng.uniform(.0025,.0083)
            value=rng.uniform(.19,.42)
            line=.5+(root-.5)*(1-.49*v)+drift*v+curve*v*v+rip*np.sin(v*6.8+root*8.5)
            fade=np.clip((length-v)*9.5,0,1)*np.clip(v*17,0,1)
            spread=fiber*(.94-.60*v)+.0014
            alpha=np.clip((spread*1.48-np.abs(u-line))*330,0,1)*fade
            shimmer=1+.24*np.exp(-((v-(.33+.14*root))/.105)**2)
            glow=np.maximum(glow,alpha*shimmer*value)
            coverage=np.maximum(coverage,alpha)
        row,col=divmod(index,4)
        tile=atlas[row*height+pad:(row+1)*height-pad,col*width+pad:(col+1)*width-pad]
        brightness=.150+glow*.28
        channels=(brightness,brightness*.68,brightness*.47)
        for i,value in enumerate(channels):
            tile[:,:,i]=value
        tile[:,:,3]=coverage
    x=np.linspace(-1,1,width,dtype=np.float32)[None,:]
    y=np.linspace(-1,1,height,dtype=np.float32)[:,None]
    r=np.sqrt(x*x+y*y)
    angle=np.arctan2(y,x)+r*.64+.08*np.sin(r*9)
    lines=(.53+.47*np.sin(angle*112+np.sin(r*12)))**2
    fine=(.5+.5*np.sin(angle*199+r*12))*.034
    brightness=.183+lines*.069+fine+.015*np.sin(angle*7+r*4)
    scalp=atlas[height:,3*width:]
    scalp[:,:,:3]=np.stack((brightness,brightness*.68,brightness*.47),axis=2)
    scalp[:,:,3]=1
    return atlas


def write_maps(folder):
    folder.mkdir(parents=True,exist_ok=True)
    radians=np.arange(SIZE,dtype=np.float32)*(np.float32(2*np.pi)/SIZE)
    x,y=radians[None,:],radians[:,None]
    rng=np.random.default_rng(16712)
    paths={}
    for surface in SURFACES:
        entries={}
        for kind,pixels in maps(x,y,rng,surface).items():
            file=folder/f"HumanSheet_{surface}_{kind}.png"
            write_png(file,pixels)
            entries[kind]=file
        paths[surface]=entries
    strands=folder/"HumanSheet_HairCards_BaseColor.png"
    write_png(strands,hair_atlas())
    paths["HairCards"]={"BaseColor":strands}
    return paths
