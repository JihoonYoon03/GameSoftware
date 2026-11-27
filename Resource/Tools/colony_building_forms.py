"""Architectural modules for the 1:1 colony building library."""
from dataclasses import dataclass
from math import cos, sin, tau

from mathutils import Vector

from build_cq04b_city_kit import Mesh

BODY, ARMOR, DARK, TRIM = "clad", "armor", "dark", "grating"
STONE, GLASS, SIGN, LIGHT, AMBER, PLANT = "bulkhead", "glass", "sign", "cyan", "amber", "bark"
BLUE, OXIDE, FOLIAGE = "blue_paint", "oxide_paint", "foliage"


class Form(Mesh):
    def block(self, x,y,z, dx,dy,dz, mat, corner=.0):
        if min(dx,dy,dz)<=.001:
            return
        a,b=x-dx/2,x+dx/2
        c,d=y-dy/2,y+dy/2
        k=min(corner,dx*.28,dy*.28)
        if not k:
            self.box(x,y,z,dx,dy,dz,mat)
        else:
            self.prism([(a+k,c),(b-k,c),(b,c+k),(b,d-k),(b-k,d),(a+k,d),(a,d-k),(a,c+k)],
                       z-dz/2,z+dz/2,mat)

    def rings(self,rings,mat,close=True):
        if close:
            for i in range(1,len(rings[0])-1):
                self.face((rings[0][0],rings[0][i+1],rings[0][i]),mat)
                self.face((rings[-1][0],rings[-1][i],rings[-1][i+1]),mat)
        for row,up in zip(rings,rings[1:]):
            for i in range(len(row)):
                k=(i+1)%len(row)
                self.face((row[i],row[k],up[k],up[i]),mat)

    def tube(self,a,b,r1,r2,mat,n=10):
        a,b=Vector(a),Vector(b)
        axis=(b-a).normalized()
        u=axis.cross(Vector((0,0,1) if abs(axis.z)<.95 else (0,1,0))).normalized()
        v=axis.cross(u)
        self.rings([[p+r*(cos(j*tau/n)*u+sin(j*tau/n)*v) for j in range(n)]
                    for p,r in ((a,r1),(b,r2))],mat)

    def profile(self,x,y,sections,mat,n=16,ratio=1):
        self.rings([[(x+radius*cos(j*tau/n),y+ratio*radius*sin(j*tau/n),z)
                   for j in range(n)] for z,radius in sections],mat)

    def beam(self,a,b,width,mat):
        self.tube(a,b,width/2,width/2,mat,4)


@dataclass(frozen=True)
class Building:
    name: str
    width: float
    depth: float
    height: float
    style: str
    floors: int
    budget: int
    door: tuple=(1.8,2.35)
    old: str=""
    exact: bool=True


NEW = (
    Building("Spaceport_Hangar_v4",28,22,12,"hangar",1,8000,(12,6.5)),
    Building("Spaceport_ControlTower_v4",10,10,24,"control",5,6500),
    Building("Security_Precinct_v4",18,14,10,"security",3,6500,(2.2,2.55)),
    Building("Colony_MidRise_Res_A_v4",14,12,16,"res_a",5,6000),
    Building("Colony_MidRise_Res_B_v4",16,13,19.2,"res_b",6,6000),
    Building("Colony_MidRise_Res_C_v4",18,14,22,"res_c",6,6000),
    Building("Colony_MarketArcade_v4",16,10,7,"market",2,5000,(4.0,2.65)),
    Building("Colony_KioskStrip_v4",12,6,4.5,"kiosk",1,3500,(2.1,2.5)),
    Building("Colony_AtmoRefinery_v4",20,16,15,"refinery",3,7000,(3.2,3.05)),
    Building("Colony_Substation_v4",12,10,7,"power",1,4500,(2.8,2.55)),
    Building("Halo_PlayerHome_v4",12,10,6,"home",2,6000,(1.65,2.3)),
    Building("Halo_Observatory_v4",14,14,18,"optics",4,8000),
    Building("Frontier_Bunker_v4",10,8,5,"bunker",1,4000,(1.85,2.3)),
    Building("Frontier_RelayTower_v4",8,8,16,"relay",2,5000),
)
REPLACEMENTS = tuple(Building(old+"_v4",w,d,n*3.2,style,n,cap,door,old) for old,w,d,n,style,cap,door in (
    ("RibDome_6p5m",13,12,2,"radome",8000,(2,2.4)),
    ("TerracedPods_10p8m",16,13,3,"res_c",8000,(1.8,2.35)),
    ("Habitat_7p2m",13,10,2,"home",8000,(1.7,2.35)),
    ("Workshop_5p4m",12,11,2,"workshop",8000,(4.5,2.65)),
    ("Observatory_11p5m",12,13,4,"optics",8000,(1.8,2.35)),
    ("Spire_Helix_12F",17,15,12,"spire",10000,(3,2.65)),
    ("Tower_TwinBridge_16F",26,17,16,"twin",12000,(4.2,2.65)),
    ("Arcology_Terrace_10F",24,20,10,"terrace",10000,(4.5,2.65)),
    ("Civic_TransitAtrium_4F",23,18,4,"atrium",9000,(6.0,2.65)),
    ("Habitat_Courtyard_3F",19,17,3,"court",8000,(3.2,2.65)),
))
STYLES = ("res_a","workshop","clinic","hangar","atrium","market","res_b",
          "spire","twin","court","workshop","hangar","relay","bunker")


def measured_specs(prior):
    result=[]
    for (key,row),style in zip(prior.items(),STYLES,strict=True):
        d,w,h=row["structure_dimensions_m"]
        port=row["entrances"][0]
        door=(port["opening_width_m"],port["opening_height_m"])
        result.append(Building(key,w,d,h,style,len(row["story_heights_m"]),12000,door,exact=False))
    return result


def shell(g, lo, hi, span, a,z0,z1,mat):
    # Exterior shell remains open through the entry and compatible with separate interiors.
    g.block((hi+lo)/2,-span+.11,(z0+z1)/2,hi-lo,.22,z1-z0,mat)
    g.block((hi+lo)/2,span-.11,(z0+z1)/2,hi-lo,.22,z1-z0,mat)
    g.block(lo+.115,0,(z0+z1)/2,.23,span*2-.44,z1-z0,mat)
    right=-span+.22
    for y0,y1,head in sorted(a):
        if y0>right:
            g.block(hi-.12,(right+y0)/2,(z0+z1)/2,.24,y0-right,z1-z0,mat)
        if z1>head:
            g.block(hi-.12,(y0+y1)/2,(head+z1)/2,.24,y1-y0,z1-head,mat)
        right=y1
    if right<span-.22:
        g.block(hi-.12,(right+span-.22)/2,(z0+z1)/2,.24,span-.22-right,z1-z0,mat)


def facade(g, dx,dy,z,rise,style,tier,cx=0,cy=0):
    g.section="Façade"
    for axis,a,b in ((0,dx,dy),(1,dy,dx)):
        bays=1 if tier==2 else max(2,min(4 if tier==0 else 3,round(b/(1.75 if tier==0 else 3.0))))
        panel=b*1.88/bays
        for side in (-1,1):
            for j in range(bays):
                p=(j+.5-bays/2)*panel
                if style in {"bunker","refinery","power","hangar","relay"} and (j+side)%2==0:
                    continue
                width=panel*(.46 if style in {"res_b","security","bunker"} else .76)
                h=rise*(.28 if style in {"bunker","hangar","refinery","relay"} else .47)
                d=.052
                x,y=(cx+side*a,cy+p) if not axis else (cx+p,cy+side*a)
                sx,sy=(d,width) if not axis else (width,d)
                center=z+rise*(.57+(.08 if style=="res_b" and j%2 else 0))
                if axis==0:
                    surface=x+side*.028
                    coords=((surface,y-width/2,center-h/2),(surface,y+width/2,center-h/2),
                            (surface,y+width/2,center+h/2),(surface,y-width/2,center+h/2))
                else:
                    surface=y+side*.028
                    coords=((x+width/2,surface,center-h/2),(x-width/2,surface,center-h/2),
                            (x-width/2,surface,center+h/2),(x+width/2,surface,center+h/2))
                g.face(coords if side>0 else coords[::-1],GLASS if tier<2 else DARK)
                if tier==0 and (style!="twin" or j%2==0):
                    q=.065*side
                    g.block(x+(q if not axis else 0),y+(q if axis else 0),center-h*.52,
                            sx+.10,sy+.085,.095,TRIM)
                    if j==bays//2 and style in {"res_a","res_c","terrace","court","home"}:
                        g.section="Balconies"
                        x+=(.29*side if not axis else 0);y+=(.29*side if axis else 0)
                        g.block(x,y,center-h*.80,.65 if not axis else width+.21,
                                width+.21 if not axis else .65,.12,ARMOR,.055)
                        g.block(x+(.27*side if not axis else 0),y+(.27*side if axis else 0),
                                center-h*.76+.32,.045 if not axis else width+.20,width+.20 if not axis else .045,.50,DARK)
                        g.section="Façade"
    g.section="Structure"


def ladder(g, x,y,z0,z1,side,tier):
    g.section="Service hardware"
    if tier==0:
        for sign in (-1,1):
            g.beam((x,y+.20*sign,z0),(x,y+.20*sign,z1),.078,TRIM)
        for i in range(int((z1-z0)/.36)):
            z=z0+i*.36+.18
            g.beam((x,y-.20,z),(x,y+.20,z),.050,TRIM)


def building(spec,tier,prior=None):
    g=Form()
    w,d,h=spec.width/2,spec.depth/2,spec.height
    smaller=tier>=2
    front=d if prior else d-min(1.20,d*.20)
    shell_back=-d if prior else -d+.65
    span=w if prior else w-.68
    floor=.06
    foundation=-prior["foundation_skirt_depth_m"] if prior else -.28
    style=spec.style
    shell_material=ARMOR if style in {"security","relay","bunker","spire"} else BODY
    entrances=[]
    if prior:
        for port in prior["entrances"]:
            y=port["threshold_xyz_m"][1]
            br=port["opening_width_m"]/2
            entrances.append((y-br,y+br,floor+port["opening_height_m"],port["kind"]))
    else:
        width,head=spec.door
        entrances.append((-width/2,width/2,floor+head,"vehicle" if width>=4 and style in {"hangar","workshop"} else "pedestrian"))
        if style=="security":
            y=span*.55
            entrances.append((y-1.8,y+1.8,3.18,"response_bay"))
    g.section="Foundation"
    g.block(0,0,(foundation+floor)/2,d*2,w*2,floor-foundation,STONE,0 if prior else .16)
    g.section="Structure"
    levels=prior["story_heights_m"] if prior else [h/spec.floors]*spec.floors
    low_roof={"hangar","power","control","relay","bunker","radome","refinery"}
    target=min(levels[0],h*.54) if style in low_roof else min(levels[0],h-.46)
    z_ground=max(target,max(p[2] for p in entrances)+.22)
    # Leave the same ground passage open at every tier, including the shadow proxy.
    shell(g,shell_back,front,span,[(a,b,top) for a,b,top,_ in entrances],floor,z_ground,shell_material)
    no_upper={"hangar","kiosk","power","bunker","control","relay","refinery","radome"}
    if style not in no_upper and tier!=3:
        facade(g,(front-shell_back)/2-.02,span,floor,z_ground-floor,style,tier,(front+shell_back)/2)
        # Ground-front window panels must never cross any measured door opening.
        for section in ("Façade","Balconies"):
            g.sections[section]=[(pts,mat) for pts,mat in g.sections[section]
                if min(p[0] for p in pts)<front-.17 or not any(
                    min(p[1] for p in pts)<b+.10 and max(p[1] for p in pts)>a-.10 and min(p[2] for p in pts)<top+.14
                    for a,b,top,_ in entrances)]
    g.section="Entrance modules"
    for a,b,top,kind in entrances:
        y=(a+b)/2
        ext=d+1.2 if prior else d
        if ext>front+.02:
            fan=((front,a,-.09),(ext,a,-.09),(ext,b,-.09),(front,b,-.09),
                 (front,a,floor),(ext,a,0),(ext,b,0),(front,b,floor))
            for ids in ((3,2,1,0),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)):
                g.face(tuple(fan[i] for i in ids),STONE)
        side=.10 if smaller else .18
        for k in (a-side/2,b+side/2):
            g.block(front-.04,k,(top+floor)/2,.42 if not smaller else .26,side,top-floor,ARMOR)
        g.block(front+.065,y,top+.106,.45,b-a+side*2,.20,TRIM)
        roof_x=(front+ext)/2
        g.block(roof_x,y,top+.27,ext-front,b-a+.47,.17,ARMOR,.1)
        if tier==0:
            g.block(roof_x,y,top+.165,.09,b-a-.12,.045,AMBER if kind!="pedestrian" else LIGHT)
            g.block(front+.25,y,top+.42,.052,(b-a)*.46,.22,SIGN)
        if kind=="vehicle" and tier<2:
            g.section="Open shutter"
            for t in range(5 if tier==0 else 2):
                z=top+.16+t*.11
                if z<h-.12:
                    g.block(front-.14,y,z,.075,b-a,.091,TRIM)
            g.section="Entrance modules"
    g.section="Structure"
    core_x=(shell_back+front)*.5
    core_d=(front-shell_back)*.5-.16
    core_w=span-.15
    roof_floor=max(.7,min(.45*levels[-1],1.5))
    max_floor=max(z_ground,h-roof_floor)
    if style in no_upper:
        feature(g,style,w,d,h,z_ground,tier)
    else:
        bottom=z_ground
        for i in range(1,len(levels)):
            top=min(max_floor,sum(levels[:i+1]))
            if top<=bottom+.08:
                continue
            inset=0
            if style in {"res_c","terrace","spire"}:
                inset=min(core_d*.51, .42*(i//2)+.33*(i//4))
            if style=="spire":
                shift=sin(i*.40)*min(.82,inset)
            else:
                shift=0
            xdim,ydim=core_d-inset,core_w-inset
            if style=="twin" and i>=3:
                split=ydim*.56
                for sign in (-1,1):
                    g.block(core_x,sign*split,(bottom+top)/2,2*xdim,ydim*.77,top-bottom,shell_material,.22 if tier<2 else 0)
                    if tier<3 and (tier<2 or i%3==0):
                        facade(g,xdim,ydim*.385,bottom,top-bottom,style,tier,core_x,sign*split)
                if i in {4,8,12,15}:
                    g.section="Cross bridges"
                    g.block(core_x,0,(bottom+top)/2,xdim*1.18,ydim*.70,(top-bottom)*.72,GLASS,.2)
                    g.section="Structure"
            elif style=="court":
                for direction in (-1,1):
                    g.block(core_x,direction*ydim*.75,(bottom+top)/2,2*xdim,ydim*.50,top-bottom,shell_material,.16)
                g.block(core_x-xdim*.81,0,(bottom+top)/2,xdim*.38,ydim,top-bottom,shell_material,.12)
                if tier<3:
                    facade(g,xdim,ydim,bottom,top-bottom,style,tier,core_x)
            else:
                g.block(core_x+shift,0,(bottom+top)/2,2*xdim,2*ydim,top-bottom,shell_material,.19 if tier<2 else 0)
                if tier<3 and (tier<2 or i%3==0):
                    facade(g,xdim,ydim,bottom,top-bottom,style,tier,core_x+shift)
            if style!="court" and tier<2 and (tier==0 or i%2==0):
                g.section="Façade"
                band=BLUE if (style in {"res_b","atrium","home"} and i%2==1) else TRIM
                g.block(core_x+shift,0,bottom+.06,2*xdim+.12,2*ydim+.12,.18,band,.19)
                g.section="Structure"
            bottom=top
        if style=="optics":
            g.section="Optical dome"
            r=min(w,d)*.76
            z0=h*.66
            # Replace the surrounding upper shell with the actual dome above the core.
            limit=min(h-.05,max(z_ground+.2,z0))
            for sec in ("Structure","Façade"):
                g.sections[sec]=[(pts,mat) for pts,mat in g.sections[sec] if max(p[2] for p in pts)<=limit+.01]
            g.block(0,0,(z_ground+z0)/2,2*r,2*r,z0-z_ground,ARMOR,.45)
            rings=([(.0,1),(.16,1.03),(.48,.92),(.78,.65),(.97,.29),(1,.11)]
                   if tier==0 else [(0,1),(.43,.92),(.81,.60),(1,.1)])
            g.profile(0,0,[(z0+(h-z0)*f,r*k) for f,k in rings],GLASS if tier<3 else DARK,32 if tier==0 else (20 if tier==1 else 10))
            if tier<2:
                g.tube((-.64*r,0,z0+.16),(r*.63,0,h*.963),.19,.135,TRIM,8)
        else:
            g.section="Roof"
            if style=="court":
                for y in (-core_w*.67,core_w*.67):
                    g.block(0,y,(max_floor+h)/2,core_d*2,core_w*.60,h-max_floor,ARMOR,.24)
                if tier<=1:
                    for y in (-core_w*.5,core_w*.5):
                        g.block(front-.54,y,z_ground+.29,.33,core_w*.25,.49,PLANT,.06)
            elif style=="twin":
                for y in (-core_w*.56,core_w*.56):
                    g.block(0,y,(max_floor+h)/2,core_d*1.76,core_w*.72,h-max_floor,ARMOR,.22)
            else:
                g.block(core_x,0,max_floor+.12,core_d*1.80,core_w*1.77,.24,ARMOR,.25)
                if style=="atrium":
                    width=core_w*.58
                    count=(16,12,8,6)[tier]
                    g.rings([[(x,cos(j*3.14159265/count)*width,max_floor+.24+
                                (h-max_floor-.24)*sin(j*3.14159265/count)) for j in range(count+1)]
                             for x in (core_x-core_d*.83,core_x+core_d*.83)],GLASS)
                else:
                    roofplant(g,h,h-max_floor-.24,core_d,core_w,style,tier)
    if tier==0:
        g.section="Service hardware"
        if style in {"res_a","res_b","res_c","terrace","home","workshop","clinic","security","kiosk","market"}:
            ladder(g,shell_back-.12,-span*.49,.35,min(z_ground-.23,h-.3),-1,tier)
            for y in (-span*.70,span*.70):
                if not any(a-.13<y<b+.13 for a,b,_,_ in entrances):
                    g.tube((front-.32,y,.31),(front-.32,y,min(z_ground-.32,h-.42)),.054,.054,TRIM,8)
        if style in {"market","kiosk","atrium","clinic","home","court"}:
            for i in (-1,1):
                y=i*span*.72
                g.block(front+.26,y,.87,.67,span*.38,.15,ARMOR,.08)
                g.block(front+.22,y,.42,.46,span*.30,.82,STONE,.06)
        if style in {"market","kiosk","security","atrium"}:
            g.block(front+.44,0,min(h-.4,z_ground+.21),.06,span*.85,.37,SIGN)
    if style in {"res_b","spire","atrium"} and tier<3:
        g.section="Service riser"
        sides=(-1,1) if style=="atrium" else (-1,)
        for sign in sides:
            y=sign*(span-.22)
            extent=z_ground if style=="atrium" else h*.77
            g.block(shell_back+.81,y,extent*.50,1.56,.38,extent,BLUE,.08)
            g.block(shell_back+.81,y+sign*.18,extent*.60,.30,.11,extent*.65,TRIM,.03)
    return g,entrances,front


def roofplant(g,h,depth,dx,dy,style,tier):
    if depth<=.02:
        return
    width=dx*(.78 if style=="atrium" else .59)
    lateral=dy*(1.45 if style in {"atrium","workshop"} else .57)
    g.block(0,dy*.12,h-depth/2,width,lateral,depth,GLASS if style in {"clinic","atrium"} else DARK,.20 if tier<2 else 0)
    if tier==0:
        g.section="Roof HVAC"
        for i in range(5):
            p=dy*.12+(i-2)*lateral*.145
            g.block(width*.5+.028,p,h-depth*.42,.09,lateral*.080,depth*.49,TRIM)
        g.tube((-.42*dx,-.40*dy,h-depth*.9),(.42*dx,-.40*dy,h-depth*.90),.066,.066,TRIM,8)


def feature(g,style,w,d,h,base,tier):
    z=max(.4,min(h-.42,base))
    n=(20,14,8,8)[tier]
    if style in {"control","relay","refinery","radome"}:
        g.section="Equipment deck"
        g.block(-d*.05,0,z-.11,d*1.78,w*1.86,.24,ARMOR,.24)
    if style in {"hangar","kiosk","bunker","power"}:
        g.section="Roof"
        if style=="hangar":
            # Raised faceted roof; sides stay readable in silhouette at a distance.
            g.rings([[(-d*.96,-w*.94,z),(-d*.96,w*.94,z),(-d*.96,w*.84,h*.72),(-d*.96,w*.56,h),(-d*.96,-w*.56,h),(-d*.96,-w*.84,h*.72)],
                     [(d*.84,-w*.94,z),(d*.84,w*.94,z),(d*.84,w*.84,h*.72),(d*.84,w*.56,h),(d*.84,-w*.56,h),(d*.84,-w*.84,h*.72)]],BODY)
            if tier<2:
                g.section="Crane rail"
                for y in (-w*.58,w*.58):
                    g.beam((-d*.86,y,z-.37),(d*.72,y,z-.37),.24,TRIM)
                g.beam((-d*.21,-w*.63,z-.48),(-d*.21,w*.63,z-.48),.26,ARMOR)
                for y in (-w*.4,w*.4):
                    g.block(d*.84,y,z+.54,.21,1.08,.12,LIGHT)
                g.section="Hangar ribs"
                for x in (-d*.73,-d*.21,d*.30,d*.68):
                    for sign in (-1,1):
                        g.beam((x,sign*(w-.51),.12),(x,sign*(w-.47),z-.1),.24,OXIDE)
                        g.beam((x,sign*(w-.47),z-.1),(x,sign*w*.79,h*.75),.23,OXIDE)
        else:
            peak=h-.99 if style=="power" else h
            deck=min(peak-.22,z)
            g.block(-d*.08,0,(deck+peak)/2,d*1.52,w*(1.77 if style=="kiosk" else 1.58),peak-deck,ARMOR,.35 if tier<2 else 0)
            if style=="power" and tier<3:
                g.section="High voltage fittings"
                for y in (-w*.47,0,w*.47):
                    g.profile(-d*.10,y,[(h-.95,.24),(h-.83,.24),(h-.80,.36),(h-.74,.18),(h-.69,.36),
                                         (h-.61,.18),(h-.54,.36),(h-.47,.12)],TRIM,n//2)
                    g.block(-d*.10,y,h-.20,.18,1.16,.40,AMBER)
            if style=="power" and tier==3:
                g.block(-d*.10,0,h-.3,.34,w*.92,.60,DARK)
            if style=="kiosk":
                g.section="Kiosk booths"
                front=d-min(1.20,d*.20)
                for y in (-w*.53,w*.53):
                    g.block(front+.015,y,z*.55,.060,w*.57,z*.41,GLASS)
                    g.block(front+.23,y,z*.36,.56,w*.62,.13,ARMOR,.04)
                    if tier<2:
                        g.block(front+.20,y,z*.82,.62,w*.63,.12,TRIM,.05)
                        g.block(front+.20,y,z*.93,.038,w*.39,.19,SIGN)
        return
    if style=="radome":
        g.section="Radar dome"
        r=min(d,w)*.75
        foot=h*.49
        g.profile(0,0,[(foot,r),(foot+.27,r*.99),(h*.79,r*.81),(h*.93,r*.48),(h,r*.09)],GLASS if tier<3 else BODY,n)
        return
    if style in {"relay","control"}:
        g.section="Tower"
        stem=min(d,w)*(.35 if style=="control" else .29)
        top=h*(.76 if style=="control" else .92)
        g.profile(-d*.12,0,[(z,stem*1.15),(top-.85,stem*.83),(top,stem*.90)],BODY,n)
        for y in (-w*.5,w*.5):
            g.beam((-d*.19,y,z),(-d*.13,y*.42,top),.31,ARMOR)
            if tier==0:
                g.beam((-d*.19,y,z+.27),(-d*.13,-y*.42,top-.75),.13,TRIM)
        if style=="control":
            g.section="Control deck"
            g.profile(-d*.12,0,[(top-.74,stem*.86),(top,stem*1.98),(h*.90,stem*2.13),(h*.93,stem*1.99)],
                      GLASS if tier<3 else DARK,n)
            g.profile(-d*.12,0,[(h*.932,stem*2.0),(h*.952,stem*1.88)],ARMOR,n)
            if tier<2:
                g.profile(-d*.12,0,[(top-.07,stem*2.015),(top+.10,stem*2.04)],BLUE,n)
                for i in range(12 if tier==0 else 8):
                    angle=tau*i/(12 if tier==0 else 8)
                    def spoke(radius,height):
                        return (-d*.12+radius*cos(angle),radius*sin(angle),height)
                    g.beam(spoke(stem*2.01,top+.04),spoke(stem*2.035,h*.931),.16,ARMOR)
        g.section="Broadcast hardware"
        g.tube((-d*.12,0,top),(-d*.12,0,h-.16),.17,.09,TRIM,max(4,n//2))
        g.block(-d*.12,0,h-.09,.28,.28,.18,AMBER)
        if tier<2:
            g.beam((-d*.12,-w*.47,h-.61),(-d*.12,w*.47,h-.61),.19,TRIM)
            g.block(-d*.12,w*.36,h-.47,.55,.56,.21,ARMOR)
            if style=="control":
                g.block(-d*.12,0,h-.31,.47,w*.82,.31,OXIDE,.08)
        return
    g.section="Cooling stacks"
    for side in (-1,1):
        y=side*w*.46
        x=-d*.37
        r=min(d,w)*.28
        g.profile(x,y,[(z,r*.92),(h*.67,r*.78),(h*.88,r),(h,r*1.05)],BODY,n)
        g.profile(x,y,[(h-.29,r*1.06),(h-.10,r*1.075)],OXIDE,n)
        if tier<2:
            g.tube((x,y,z*.25),(x*.31,y,z*.25),.23,.23,TRIM,n//2)
            g.tube((x*.31,y,z*.25),(x*.31,y,h*.63),.23,.23,TRIM,n//2)
    g.section="Plant room"
    g.block(d*.22,0,(z+h*.60)/2,d*.92,w*1.70,h*.60-z,ARMOR,.35)
