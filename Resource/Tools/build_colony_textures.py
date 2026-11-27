"""Author tileable colony surfaces and continuous spherical presentation maps."""
from pathlib import Path
import hashlib
import json
import numpy as np

from build_shared_effect_textures import noise
from human_sheet_maps import write_png

ROOT = Path(__file__).resolve().parents[1]
MAPS = ROOT/"Export/Textures/ColonyV4"
RECORDS = {}
TAU = 2*np.pi
FAMILIES = (
    "Colony_GlassCurtain", "Colony_WeatheredCladding", "Industrial_GratingTrim",
    "Interior_Bulkhead", "Storefront_SignageAtlas", "Alien_FoliageBark",
)


def save(name, image, colorspace, mapping):
    path = MAPS/(name+".png")
    write_png(path, image)
    h,w,c = image.shape
    assert path.stat().st_size < 100_000_000 and w*h*4 < 100_000_000
    RECORDS[name] = dict(path=path.relative_to(ROOT).as_posix(), size=[w,h], channels=c,
                         colorspace=colorspace, mapping=mapping, bytes=path.stat().st_size,
                         sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def step(a,b,v):
    t=np.clip((v-a)/(b-a),0,1)
    return t*t*(3-2*t)


def rgb(v):
    return np.asarray(v,dtype=np.float32)/255


def blend(c1,c2,v):
    v=v[...,None]
    return c1+(c2-c1)*v


def normal(height, power, lat=None):
    dx=(np.roll(height,-1,1)-np.roll(height,1,1))*.5
    dy=np.gradient(height,axis=0) if lat is not None else (np.roll(height,-1,0)-np.roll(height,1,0))*.5
    if lat is not None:
        dx*=np.clip(np.cos(lat),0,1)
    nx,ny = -power*dx,power*dy
    z = 1/np.sqrt(1+nx**2+ny**2)
    return .5+.5*np.stack((nx*z,ny*z,z),axis=-1)


def surfaces():
    samples=(np.arange(1024,dtype=np.float32)+.5)/1024
    u,v=np.meshgrid(samples,samples)
    for i,name in enumerate(FAMILIES):
        rng=np.random.default_rng(589+i)
        grain=noise(rng,u,v,161,157)-.5
        patch=noise(rng,u,v,12,8)
        streak=noise(rng,u,v,69,7)
        seams=step(.88,.994,np.cos(TAU*(u*(3 if i==2 else 2))))
        cross=step(.86,.998,np.cos(TAU*v*(8 if i==2 else 2)))
        seams=np.maximum(seams,cross)
        emit=np.zeros_like(u)
        if i==0:
            height=-.105*seams+.009*grain
            base=blend(rgb((25,46,56)),rgb((78,118,130)),.26*patch+.42*streak)
            emit=.18*(1-seams)*step(.64,.84,patch)
            metal,rough=.45,.23
        elif i==1:
            height=-.15*seams+.027*grain+.037*patch
            base=blend(rgb((92,92,86)),rgb((190,191,181)),.58*patch+.22*streak)
            metal,rough=.58,.57
        elif i==2:
            bars=.5+.5*np.cos(TAU*(18*u+36*v))
            kerf=step(.55,.94,bars)*step(.25,.55,.5+.5*np.cos(TAU*(18*u-36*v)))
            height=.11*kerf-.11*seams+.016*grain
            base=blend(rgb((37,45,50)),rgb((113,128,129)),.23*patch+.49*kerf)
            metal,rough=.7,.46
        elif i==3:
            bevel=step(.49,.80,np.maximum(np.cos(4*np.pi*u),np.cos(4*np.pi*v)))
            height=.12*bevel-.13*seams+.014*grain
            base=blend(rgb((98,114,117)),rgb((173,187,182)),.32*patch+.39*bevel)
            metal,rough=.32,.68
        elif i==4:
            # Four labelled-free light tiles. Typography belongs to the game.
            x,y=(u*2)%1,(v*2)%1
            lip=np.minimum(np.minimum(x,1-x),np.minimum(y,1-y))
            field=step(.11,.16,lip)
            graphic=np.maximum(step(.93,.999,np.cos(TAU*(x*4+y))),step(.12,.14,lip)-step(.16,.19,lip))
            emit=field*(.36+.58*graphic)
            base=blend(rgb((22,30,34)),rgb((92,142,148)),emit)
            height=.065*field+.012*grain
            metal,rough=.22,.42
        else:
            seams=np.zeros_like(u)
            furrow=.5+.5*np.cos(TAU*(17*u+2.1*streak+.23*np.sin(TAU*v*4)))
            height=.086*furrow+.021*grain+.029*patch
            base=blend(rgb((43,42,38)),rgb((125,117,92)),.57*furrow+.32*patch)
            metal,rough=0,.77
        base=np.clip(base*(1-.28*seams[...,None])+grain[...,None]*.028,0,1)
        orm=np.stack((1-.35*seams-.15*(1-patch),rough+.12*(patch-.5),np.full_like(u,metal)),axis=-1)
        emission=emit[...,None]*rgb((99,231,218))
        for suffix,img,space in (("BaseColor",base,"sRGB"),("Normal",normal(height,26),"linear"),
                                 ("ORM",orm,"linear"),("Emissive",emission,"sRGB")):
            save(name+"_"+suffix,img,space,"2x2 atlas" if i==4 else "repeat UV")


def spherical_field(x,y,z,freq,seed,count):
    rng=np.random.default_rng(seed)
    field=np.zeros_like(x)
    for _ in range(count):
        heading=rng.normal(size=3)
        heading/=np.linalg.norm(heading)
        a,b,c=heading*freq*rng.uniform(.72,1.36)
        field+=np.sin(a*x+b*y+c*z+rng.uniform(0,TAU))
    return field/np.sqrt(count*1.4)


def planet(name,w,h,seed):
    lat=(.5-(np.arange(h,dtype=np.float32)+.5)/h)[:,None]*np.pi
    lon=((np.arange(w,dtype=np.float32)+.5)/w)[None,:]*TAU
    x,y,z=np.cos(lat)*np.cos(lon),np.cos(lat)*np.sin(lon),np.broadcast_to(np.sin(lat),(h,w))
    field=lambda fr,offset,n=6:spherical_field(x,y,z,fr,seed+offset,n)
    broad=field(5.5,0,10)
    range_=field(22,1,7)
    fine=.32*field(75,2,6)+.14*field(260,3,5)+.055*field(700,4,4)
    if name=="Erma":
        land=.65*broad+.25*range_+.15*fine+.13*np.sin(2.4*z)
        relief=np.maximum(land+.025,0)*(.45+.47*step(.15,.72,range_))+.09*fine*step(-.07,.12,land)
        biome=step(.19,.56,abs(z)+.24*range_)
        ground=blend(rgb((63,100,82)),rgb((147,129,98)),biome)
        ground=blend(ground,rgb((163,156,136)),step(.26,.58,relief)*.63)
        ocean=blend(rgb((12,38,63)),rgb((36,103,119)),step(-.42,.04,land))
        color=blend(ocean,ground,step(-.01,.035,land))
        color=blend(color,rgb((208,219,212)),step(.80,.98,abs(z)+.095*range_)*.96)
        clouds=step(.29,.64,.71*field(27,8)+.31*field(76,9)+.16*field(152,10)+.21*np.cos(18*z+3*broad))
        clouds*=step(.03,.2,1-abs(z))
        save("Planet_Erma_Cloud",np.stack((np.ones_like(z)*.91,np.ones_like(z)*.94,np.ones_like(z),clouds),axis=-1),
             "sRGB color, linear opacity","equirectangular")
    elif name=="Seira":
        ravine=(1-abs(field(38,5)))*step(-.40,.14,broad)
        relief=.58*broad+.24*range_-.13*ravine+.19*fine
        color=blend(rgb((81,50,45)),rgb((186,140,97)),step(-.57,.61,relief))
        color=blend(color,rgb((139,103,112)),step(.49,.83,range_)*.3)
    else:
        relief=.45*broad+.19*range_+.07*fine
        ice=step(.31,.72,abs(z)+.25*broad+.16*range_)
        color=blend(rgb((106,77,64)),rgb((198,155,102)),step(-.66,.6,relief))
        color=blend(color,rgb((183,210,215)),ice)
    color=np.clip(color*(1+.12*fine[...,None]),0,1)
    assert np.mean(np.abs(color[:,0,:]-color[:,-1,:])) < .028
    save("Planet_"+name+"_Albedo",color,"sRGB","equirectangular")
    save("Planet_"+name+"_Normal",normal(relief,30,lat),"linear","equirectangular / tangent +Y")


def sky():
    u,v=np.meshgrid((np.arange(2048)+.5)/2048,(np.arange(1024)+.5)/1024)
    rng=np.random.default_rng(26)
    ridge=v-.48-.14*np.sin(u*TAU+.82)-.066*np.sin(2*u*TAU)
    n=noise(rng,u,v,16,13)
    dust=np.exp(-12*abs(ridge+(.5-n)*.09))
    filament=dust**2.4*(.50*noise(rng,u,v,60,43)+.33*n+.17*noise(rng,u,v,152,111))
    color=blend(rgb((3,7,17)),rgb((46,56,112)),dust*n*.85)
    color=blend(color,rgb((106,57,117)),step(.28,.60,filament))
    color+=filament[...,None]**2*rgb((28,65,93))
    iy=rng.integers(2,1022,6000)
    ix=rng.integers(0,2048,6000)
    strength = .13+.82*rng.random(len(ix))**3.5
    star=strength[...,None]*blend(rgb((151,190,255)),rgb((255,233,191)),rng.random(len(ix)))
    color[iy,ix]+=star
    for dy,dx in ((1,0),(-1,0),(0,1),(0,-1)):
        color[iy+dy,(ix+dx)%2048]+=star*.11
    save("Space_Starfield_Nebula",np.clip(color,0,1),"sRGB","equirectangular sky")
    u,v=np.meshgrid((np.arange(1024)+.5)/1024,(np.arange(256)+.5)/256)
    gaps=(1-.92*np.exp(-((u-.43)/.015)**2))*(1-.84*np.exp(-((u-.72)/.011)**2))
    bands=(.56+.26*np.sin(u*TAU*14)+.11*np.sin(u*TAU*43)+.07*np.sin(u*TAU*113))
    alpha=step(.02,.12,u)*(1-step(.81,.99,u))*bands*gaps*(.83+.17*np.cos(TAU*v))
    save("Planet_Orren_Ring",np.concatenate((blend(rgb((141,148,157)),rgb((206,179,138)),u),alpha[...,None]),axis=-1),
         "sRGB color, linear opacity","U radial / V azimuth")


if __name__=="__main__":
    MAPS.mkdir(parents=True,exist_ok=True)
    surfaces()
    for name,w,h,seed in (("Erma",4096,2048,40617),("Seira",2048,1024,3857),("Orren",2048,1024,772)):
        planet(name,w,h,seed)
    sky()
    assert len(RECORDS)==33
    manifest=dict(request="7.6.2", max_texture_bytes=100_000_000, orm="R occlusion / G roughness / B metallic",
                  planetary_role="presentation; not the gameplay elevation or navigation grid",textures=RECORDS)
    (ROOT/"Documentation/colony_v4_textures.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(f"Wrote {len(RECORDS)} maps; largest {max(row['bytes'] for row in RECORDS.values())} bytes")
