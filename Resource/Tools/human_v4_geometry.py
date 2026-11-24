"""Parametric human surface and garment primitives in HUMAN_7H bind space."""
from dataclasses import dataclass
from math import copysign, cos, pi, sin, tau

from mathutils import Vector


@dataclass(frozen=True)
class Surface:
    slot: str
    tint: str


def fabric(tint):
    return Surface("Fabric", tint)


def shell(tint):
    return Surface("Shell", tint)


SKIN = Surface("Skin", "skin")
GLASS = Surface("Visor", "glass")


def ease(a,b,t):
    return a+(b-a)*t


def blend_weights(name, adjacent, amount):
    return {name: 1-amount, adjacent: amount} if adjacent and amount else {name: 1}


def samples(count, stops):
    """Sample authored profile landmarks, preserving endpoints and major cross sections."""
    grid = sorted({0.,1.,*(i/(count-1) for i in range(1,count-1))})
    for t in grid:
        for (a,*pa),(b,*pb) in zip(stops,stops[1:]):
            if t <= b:
                yield t, tuple(ease(x,y,(t-a)/(b-a)) for x,y in zip(pa,pb))
                break


class Geometry:
    def __init__(self, detail=False):
        self.v, self.faces, self.weights, self.materials, self.smooth = [], [], [], [], []
        self.seam_normals = {}
        self.vertex_uv = {}
        self.face_uv = {}
        self.detail = detail

    def poly(self, indices, material, smooth=False, uv=None):
        if uv is not None:
            self.face_uv[len(self.faces)] = tuple(uv)
        self.faces.append(tuple(indices))
        self.materials.append(material)
        self.smooth.append(smooth)

    def loft(self, centers, radii, section, material, weights,
             axis=(0,0,1), power=1., distortion=None, caps=True):
        if self.detail:
            section *= 2
        axis = Vector(axis).normalized()
        x = (Vector((1,0,0))-axis*axis.x).normalized()
        y = axis.cross(x).normalized()
        base = len(self.v)
        for level,(center, (w,d)) in enumerate(zip(centers,radii)):
            center = Vector(center)
            for i in range(section):
                angle = tau*i/section
                c,s = cos(angle),sin(angle)
                coord = center + x*copysign(abs(c)**power*w,c) + y*copysign(abs(s)**power*d,s)
                if distortion:
                    coord = distortion(coord, level, angle)
                self.v.append(coord)
                self.weights.append(weights[level] if isinstance(weights, list) else weights)
        close_bottom, close_top = (caps, caps) if isinstance(caps, bool) else caps
        if close_bottom:
            self.poly(reversed(range(base,base+section)),material)
        for j in range(len(centers)-1):
            for i in range(section):
                a,b = base+j*section+i,base+j*section+(i+1)%section
                self.poly((a,b,b+section,a+section),material,True)
        if close_top:
            self.poly(range(base+(len(centers)-1)*section,base+len(centers)*section),material)
        return self

    def ellipsoid(self, center, radius, mat, bone, section=12, rings=6):
        """Closed quad loft with smoothly rounded poles and bounded triangle count."""
        x,y,z = center
        w,d,h = radius
        levels = [-pi/2+.10 + j*(pi-.20)/(rings-1) for j in range(rings)]
        return self.loft([(x,y,z+h*sin(t)) for t in levels],
                         [(w*cos(t),d*cos(t)) for t in levels],section,mat,{bone:1})

    def panel(self, center, size, mat, bone, section=8, angle=0.):
        x,y,z = center
        width,depth,height = size
        from mathutils import Quaternion
        q = Quaternion((0,1,0),angle)
        levels = (0, .10,.90,1.)
        values = ((.90,.93),(1,1),(1,1),(.89,.91))
        base = len(self.v)
        self.loft([(0,0,(t-.5)*height) for t in levels],
                  [(width*.5*w,depth*.5*d) for w,d in values],section,mat,{bone:1},power=.32)
        for i in range(base,len(self.v)):
            self.v[i] = q@self.v[i]+Vector((x,y,z))
        return self

    def box(self, center, size, mat, bone):
        x,y,z=center
        w,d,h=(n/2 for n in size)
        base=len(self.v)
        self.v.extend(Vector((x+i*w,y+j*d,z+k*h)) for k in (-1,1)
                      for i,j in ((-1,-1),(1,-1),(1,1),(-1,1)))
        self.weights.extend([{bone:1}]*8)
        for f in ((3,2,1,0),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)):
            self.poly((i+base for i in f),mat)
        return self


# Shared shape parameters combine the repository's physical fit with the six new sheets.
ROLES = {
    "Explorer": (.018,"slate","bone","amber"),
    "Guard": (.022,"coal","blueplate","ice"),
    "Medic": (.016,"olive","sand","signal"),
    "Pilot": (.010,"flight","bone","amber"),
    "Civilian": (.013,"ochre","twill","sand"),
    "Engineer": (.017,"ochre","coal","amber"),
    "Researcher": (.017,"plum","bone","ice"),
    "Miner": (.023,"slate","blueplate","amber"),
    "HeavyCombat": (.039,"coal","blueplate","signal"),
}


def torso(p, lod, garment=None, region="full", detail=False):
    """Shape waist, ribcage and trapezius as a single surface per original part."""
    bulk,tint,armor,accent = ROLES.get(garment,(0,"","", ""))
    g = Geometry()
    dress = fabric(tint) if garment else SKIN
    rib, waist, hip = p["rib"], p["waist"], p["hip"]
    # z, radius X, anterior depth, posterior depth. Include the heavy build's lower abdomen.
    landmarks = [
        (1.002,hip*.89,p["belly"]*.87,p["back"]*.95),
        (1.087,waist*1.03,p["belly"],p["back"]),
        (1.168,waist*.97,p["belly"]*.93,p["back"]*.91),
        (1.252,rib*.92,p["front"]*.97,p["back"]*.95),
        (1.335,rib,p["front"]+p["bust"]*.34,p["back"]),
        (1.406,p["shoulder"]*.98,p["front"]*.78,p["back"]*.92),
        (1.449,p["shoulder"]*.73,p["front"]*.62,p["back"]*.74),
        (1.476,.073,.065,.069),
    ]
    if region=="full" and garment in ("Civilian","Pilot"):
        # The port jacket has a straight hem; the flight layer stays close to the hips.
        z,w,f,b=landmarks[0]
        landmarks[0]=(z,w*(1.06 if garment=="Civilian" else .94),f,b)
    if region=="full" and lod>=2:
        landmarks = [landmarks[i] for i in (0,2,5,7)]
    elif region=="full" and lod==1:
        landmarks = [landmarks[i] for i in (0,1,3,5,7)]
    section = 28 if detail else (16,12,8)[lod]
    centers = [(0,(back-front)/2,z) for z,w,front,back in landmarks]
    radii = [(w+bulk,(front+back)/2+bulk) for z,w,front,back in landmarks]
    weights = [blend_weights("chest","spine",max(0,min(1,(1.33-z)/.27))) for z,*_ in landmarks]
    def shape(point, level, theta):
        if point.y<0:
            point.y -= p["bust"]*.21 * max(0,1-abs(abs(point.x)-rib*.46)/.082) * max(0,1-abs(point.z-1.34)/.10)
        return point
    if region=="full":
        g.loft(centers,radii,section,dress,weights,power=.84,distortion=shape)
    else:
        seam=3
        lower=region=="abdomen"
        span=slice(None,seam+1) if lower else slice(seam,None)
        g.loft(centers[span],radii[span],section,dress,weights[span],power=.84,
               distortion=shape,caps=(lower,not lower))
        guide=Geometry().loft(centers[2:5],radii[2:5],section,dress,weights[2:5],
                              power=.84,distortion=shape,caps=False)
        for i in range(section):
            around=guide.v[section+(i+1)%section]-guide.v[section+(i-1)%section]
            along=guide.v[2*section+i]-guide.v[i]
            g.seam_normals[(seam*section if lower else 0)+i]=around.cross(along).normalized()
    if garment and lod<=1:
        # Contrast at the collar is structural. Most seams stay in the shared weave.
        r=.080+bulk*.28
        h = 1.515 if garment in ("Explorer","Guard","Miner","HeavyCombat") else 1.496
        g.loft([(0,0,1.467),(0,0,h-.009),(0,0,h)],[(r,r*.94),(r*.97,r*.89),(r*.86,r*.80)],
               12 if lod==0 else 8,shell(armor),{"chest":1},caps=False)
        if lod==0:
            if garment=="Civilian":
                g.panel((0,-p["front"]-bulk-.007,1.282),(.015,.016,.249),shell("coal"),"chest",6)
                for side in (-1,1):
                    g.panel((side*rib*.53,-p["belly"]-bulk+.013,1.12),(.062,.017,.054),
                            fabric("twill"),"spine",6,angle=side*.13)
            else:
                g.panel((0,-p["belly"]-bulk,1.100),(.035,.012,.072),shell(accent),"spine",6)
    return g


def pelvis(p,lod,detail=False):
    g = Geometry(detail)
    w, d = p["hip"],p["back"]
    return g.loft([(0,-.003,.91),(0,-.011,.976),(0,(d-p["belly"])*.19,1.055),(0,0,1.087)],
                  [(w*.80,d*.82),(w,d+ .016),(w*.88,d+.011),(p["waist"]*1.06,d*.88)],
                  (16,10,8)[lod],fabric("coal"),{"pelvis":1},power=.84)


def limb(rig,p,slot,side,lod,garment=None,detail=False):
    g=Geometry(detail)
    bone,pair = {"UpperArm":("upper_arm","forearm"),"Forearm":("forearm","upper_arm"),
                 "Thigh":("thigh","shin"),"Shin":("shin","thigh")}[slot]
    bone,pair = bone+"."+side,pair+"."+side
    start,end = rig.bones[bone].head_local,rig.bones[bone].tail_local
    axes = end-start
    scale = p["limb"]
    bulk,cloth,armor,accent = ROLES.get(garment,(0,"","",""))
    pad = bulk*(.59 if slot in ("Thigh","Shin") else .52)
    if garment=="Pilot":
        pad *= .85
    if slot=="UpperArm":
        radii = [(0,.054,.057),(.12,.065,.071),(.31,.064,.064),(.68,.048,.052),(1,.047,.047)]
    elif slot=="Forearm":
        radii = [(0,.047,.047),(.23,.054,.050),(.52,.045,.044),(.82,.035,.034),(1,.032,.031)]
    elif slot=="Thigh":
        radii = [(0,.080,.075),(.21,.087,.088),(.52,.073,.077),(.83,.060,.060),(1,.056,.056)]
        scale *= 1.12 if p["sex"]=="female" else 1.
    else:
        radii = [(0,.056,.056),(.23,.066,.069),(.42,.063,.061),(.74,.045,.044),(1,.037,.038)]
        scale *= 1.12 if p["sex"]=="female" else 1.
    centers,sections,weights=[],[],[]
    for t, (r,d) in samples(11 if detail else (7,5,3)[lod],radii):
        u=t
        if slot in ("UpperArm","Thigh") and t==1:
            u+=.05
        if slot in ("Forearm","Shin") and t==0:
            u-=.05
        c = start + axes*u
        if slot=="UpperArm":
            c.y-=.009*sin(pi*t)
            sign=1 if side=="L" else -1
            join=max(0.,1-t/.30)
            c.x+=sign*(min(.203,p["shoulder"]*.83+bulk*.48)-abs(start.x))*join
        if slot=="Shin": c.y+=.024*sin(pi*t)
        centers.append(c)
        sections.append((r*scale+pad,d*scale+pad))
        near = t if slot in ("Forearm","Shin") else 1-t
        if slot=="UpperArm" and t<.30:
            weights.append(blend_weights(bone,"clavicle."+side,.72*(1-t/.30)))
        else:
            weights.append(blend_weights(bone,pair,max(0,.5-1.85*near)))
    if slot in ("Thigh","Shin") and garment in ("Civilian","Medic"):
        cloth="twill" if garment=="Civilian" else "olive"
    g.loft(centers,sections,(14,10,7)[lod],fabric(cloth) if garment else SKIN,
           weights,axis=axes,power=.90)
    armoured = garment in ("Guard","Explorer","Miner","HeavyCombat")
    if armoured and lod<2 and slot in ("Forearm","Shin"):
        # Armour returns over the cloth instead of floating in front of it.
        c=start.lerp(end,.51)
        thickness = .027 if garment in ("Guard","Explorer","Miner","HeavyCombat") else .016
        c.y-=sections[len(sections)//2][1]-.006
        g.panel(c,(sections[len(sections)//2][0]*1.31,thickness,axes.length*.63),
                shell(armor if garment!="Civilian" else "twill"),bone,8 if lod==0 else 6,
                angle=axes.to_track_quat("Z","Y").to_euler().y)
    if garment=="Pilot" and lod<2:
        c=start.lerp(end,.47)
        sign=1 if side=="L" else -1
        c.x+=sign*sections[len(sections)//2][0]*.76
        g.panel(c,(.023,.044,axes.length*.64),fabric("ochre"),bone,6,
                angle=axes.to_track_quat("Z","Y").to_euler().y)
    if garment and lod==0 and slot in ("UpperArm","Thigh"):
        c=start.lerp(end,.36)
        c.y-=sections[len(sections)//2][1]-.004
        if armoured or (garment in ("Pilot","Civilian") and slot=="Thigh"):
            width=sections[len(sections)//2][0]*1.37
            if garment=="Civilian":
                c.x+=(1 if side=="L" else -1)*width*.19
                c.y+=.014
            g.panel(c,(width*(.71 if garment=="Civilian" else 1),.019,axes.length*.43),
                    shell(armor) if armoured else fabric("twill" if garment=="Civilian" else "flight"),bone,8)
            if garment in ("Explorer","Guard","Pilot"):
                g.box(c+Vector((0,-.013,.009)),(width*.50,.006,.018),shell(accent),bone)
    return g


NECK_END=(0,.007,1.565)
NECK_RADIUS=(.046,.052)
HEAD_SECTIONS=(14,10,8)


def neck_join_normals(g,start,count):
    for i in range(start,start+count):
        direction=g.v[i]-Vector(NECK_END)
        direction.z=-.010
        g.seam_normals[i]=direction.normalized()


def neck(lod,detail=False):
    g=Geometry(detail)
    section=HEAD_SECTIONS[lod]
    g.loft([(0,0,1.438),(0,.001,1.474),(0,.003,1.530),NECK_END],
           [(.061,.062),(.058,.055),(.049,.051),NECK_RADIUS],
           section,SKIN,[{"chest":.58,"neck":.42},{"neck":1},
                        {"neck":.77,"head":.23},{"head":1}],power=.92,caps=(True,False))
    count=section*(2 if detail else 1)
    neck_join_normals(g,len(g.v)-count,count)
    return g


def head(kind,lod,detail=False):
    w,d = {"Oval":(.093,.093),"Angular":(.099,.092),"Narrow":(.086,.088),"Round":(.100,.098)}[kind]
    g=Geometry(detail)
    landmarks = [(1.548,.39,.55,.004),(1.573,.61,.73,-.010),(1.607,.79,.88,-.008),
                 (1.642,.95,.95,-.004),(1.676,1,1,0),(1.707,1,.99,.002),
                 (1.742,.96,1,.007),(1.772,.79,.83,.007),(1.795,.39,.46,.004),
                 (1.800,.05,.05,.003)]
    take = (range(10), (0,1,3,5,7,9),(0,2,4,6,8,9))[lod]
    values=[landmarks[i] for i in take]
    def face(point,level,theta):
        z=point.z
        if point.y<-.030:
            ridge=max(0,1-abs(point.x)/.029)
            point.y-=ridge*.026*max(0,1-abs(z-1.652)/.044)
            if .018 < abs(point.x)<.062:
                mask=max(0,1-abs(z-1.68)/.030)
                point.y=ease(point.y,-.080,mask*.80)
        return point
    g.loft([NECK_END,*[(0,y,z) for z,a,b,y in values[1:]]],
           [NECK_RADIUS,*[(w*a,d*b) for z,a,b,y in values[1:]]],
           HEAD_SECTIONS[lod],SKIN,{"head":1},power=.92,distortion=face,caps=(False,True))
    neck_join_normals(g,0,HEAD_SECTIONS[lod]*(2 if detail else 1))
    if lod==0:
        for sign in (-1,1):
            g.ellipsoid((sign*w*.93,.006,1.660),(.015,.021,.029),SKIN,"head",8,4)
    return g


def face(style,lod,detail=False):
    g=Geometry(detail)
    if lod>=2:
        return g
    height=.014 if style=="Alert" else .011 if style=="Neutral" else .008
    for side in (-1,1):
        g.ellipsoid((side*.035,-.083,1.681),(.019,.008,height*.5),shell("ivory"),"head",8,3)
        g.ellipsoid((side*.035,-.090,1.681),(.006,.0035,height*.45),shell("coal"),"head",7,3)
        g.box((side*.037,-.083,1.701),(.034,.008,.005),fabric("hair"),"head")
    g.box((0,-.086,1.600),(.032,.006,.004),shell("lip"),"head")
    return g


def hair(style,lod,detail=False):
    g=Geometry(detail)
    c=(0,.005,1.745)
    n=(14,10,7)[lod]
    rows=[(1.666,.105,.103),(1.714,.110,.108),(1.756,.108,.103),
          (1.791,.089,.088),(1.815,.030,.035)]
    def sweep(p,i,theta):
        if i==0:
            p.z += .078*max(0,-sin(theta)) + .014*cos(theta+.6)
        elif i<4 and style not in ("Buzz","HatClearance_Buzz"):
            p.z+=.012*sin(3*theta+i*.4)
        return p
    material=fabric("rustHair" if style in ("Ponytail","Curls") else "hair")
    g.loft([(c[0],c[1],z) for z,w,d in rows],[(w,d) for z,w,d in rows],
           n,material,{"head":1},distortion=sweep,caps=(False,True))
    if style in ("Bun","Ponytail") and lod<2:
        g.ellipsoid((-.013,.091,1.755),(.055,.064,.061),material,"head",10,5)
        if style=="Ponytail" and lod==0:
            g.loft([(.0,.14,1.62),(.0,.167,1.67),(-.009,.141,1.73)],
                   [(.011,.013),(.032,.027),(.032,.037)],9,material,{"head":1})
    if style in ("Curls","SidePart","Bob") and lod==0:
        for i in range(3):
            g.ellipsoid((-.056+.036*i,-.041+ i*.014,1.772+i*.010),
                        (.045,.065,.039),material,"head",8,4)
        if style=="Bob":
            g.loft([(0,.066,1.591),(0,.057,1.635),(0,.050,1.70)],
                   [(.074,.023),(.094,.037),(.096,.039)],9,material,{"head":1})
    return g


def hand(side,lod,detail=False):
    g=Geometry(detail)
    sign=1 if side=="L" else -1
    bone="hand."+side
    g.loft([(sign*.462,-.027,.851),(sign*.451,-.021,.876),
            (sign*.444,-.017,.907),(sign*.43,-.015,.942)],
           [(.027,.021),(.033,.023),(.031,.021),(.027,.020)],(10,8,7)[lod],SKIN,{bone:1})
    if lod==0:
        for i in range(4):
            x=sign*.459+(i-1.5)*.0135
            g.loft([(x+sign*.009,-.040,.814+abs(i-1.5)*.006),(x+sign*.006,-.030,.837),(x,-.025,.864)],
                   [(.005,.006),(.007,.007),(.007,.008)],6,SKIN,{bone:1})
        g.loft([(sign*.421,-.049,.852),(sign*.418,-.041,.871),(sign*.435,-.025,.897)],
               [(.009,.008),(.012,.010),(.013,.013)],7,SKIN,{bone:1})
    return g


def boot(side,lod,detail=False):
    g=Geometry(detail)
    bone="foot."+side
    x=.115 if side=="L" else -.115
    g.loft([(x,-.064,.001),(x,-.068,.025),(x,-.066,.050)],
           [(.059,.129),(.062,.134),(.059,.126)],(14,10,8)[lod],fabric("rubber"),{bone:1},power=.68)
    g.loft([(x,-.062,.046),(x,-.054,.077),(x,-.022,.121),(x,-.005,.170)],
           [(.059,.125),(.057,.115),(.052,.077),(.048,.052)],
           (14,10,8)[lod],shell("bone"),{bone:1},power=.80)
    if lod<2:
        g.panel((x,-.037,.142),(.068,.108,.040),fabric("coal"),bone,8)
        if lod==0:
            g.box((x,-.115,.099),(.074,.019,.012),shell("amber"),bone)
    return g


def outer(p,role,lod,detail=False):
    """Use the existing outer assembly boundary for the structural layers of each outfit."""
    g=Geometry(detail)
    bulk,cloth,armor,accent=ROLES[role]
    y=-p["front"]-bulk-p["bust"]*.27
    w=p["rib"]
    panel_steps=(8,6,0)[lod]
    if lod>=2:
        if role in ("Explorer","Guard","Miner","HeavyCombat"):
            g.loft([(0,y+.010,1.219),(0,y-.021,1.273),(0,y-.035,1.349),(0,y+.002,1.401)],
                   [(w*.53,.023),(w*.76,.031),(w*.83,.036),(w*.57,.022)],6,
                   shell(armor),[{"spine":.45,"chest":.55},{"spine":.25,"chest":.75},
                                 {"chest":1},{"chest":1}],power=.66)
        return g
    if role=="Civilian":
        if lod==0:
            for side in (-1,1):
                g.panel((side*w*.42,y+.012,1.299),(w*.41,.016,.096),fabric("ochre"),"chest",6)
        return g
    if role=="Pilot":
        for side in (-1,1):
            g.panel((side*p["waist"]*.75,-p["belly"]-.012,1.071),(.040,.019,.142),
                    fabric("bone"),"pelvis",6,angle=side*.23)
            g.panel((side*p["hip"]*.64,-p["belly"]*.78-.018,.955),(.044,.019,.158),
                    fabric("rubber"),"thigh."+("L" if side>0 else "R"),6,angle=-side*.30)
        return g
    if role in ("Explorer","Guard","HeavyCombat","Miner"):
        # Chest follows the rib cage. Lower plates track spine under a different motion.
        for side in (-1,1):
            cx=side*w*.45
            g.panel((cx,y-.012,1.337),(w*.83,.041,.165),shell(armor),"chest",
                    panel_steps,angle=side*.085)
        count=3 if role in ("Guard","HeavyCombat","Miner") else 2
        for index in range(count if lod==0 else 1):
            z=1.237-.044*index
            front=ease(p["front"],p["belly"],index*.31)+bulk
            g.panel((0,-front-.011,z),(w*(1.54-.13*index),.028,.046),shell(armor),"spine",6)
    elif role in ("Medic","Researcher","Engineer"):
        for side in (-1,1):
            # Segmented side flaps are part of the existing outfit assembly and follow thighs.
            g.panel((side*p["hip"]*.58,-p["belly"]*.75-.020,1.007 if role!="Researcher" else .973),
                    (p["hip"]*.83,.026,.24 if role!="Researcher" else .32),
                    fabric(armor),"thigh."+("L" if side>0 else "R"),panel_steps,angle=side*.060)
    for side in (-1,1):
        x=side*w*.59
        strap = fabric("rubber") if role!="Medic" else fabric("twill")
        if lod==0:
            g.panel((x,y-.004,1.368),(.036,.016,.183),strap,"chest",6,angle=-side*.23)
        else:
            g.box((x,y-.004,1.368),(.035,.016,.183),strap,"chest")
        if lod==0:
            g.box((x,y-.016,1.409),(.034,.015,.026),shell(accent),"chest")
    if lod==0:
        g.box((w*.42,y-.029,1.34),(.054,.008,.014),shell(accent),"chest")
    return g


EQUIPMENT = {
    "Accessory_LifeSupport_TwinTank": ("twin", "bone", "amber"),
    "Accessory_Navigation_Console": ("console", "bone", "amber"),
    "Accessory_Wrist_Scanner_R": ("scanner", "coal", "amber"),
    "Accessory_Wrist_Scanner_L": ("scanner", "coal", "amber"),
    "Accessory_Belt_LifeSupport": ("belt", "bone", "amber"),
    "Accessory_Ablative_RibPlate": ("plate", "blueplate", "ice"),
    "Accessory_Shoulder_Tactical_R": ("shoulder", "blueplate", "sand"),
    "Accessory_Shoulder_Tactical_L": ("shoulder", "blueplate", "sand"),
    "Accessory_Combat_Reactor": ("case", "blueplate", "ice"),
    "Accessory_Medic_RescuePack": ("case", "sand", "signal"),
    "Accessory_Medic_CartridgeRack": ("rack", "sand", "amber"),
    "Accessory_Medic_Pouch_R": ("pouch", "sand", "signal"),
    "Accessory_Medic_Pouch_L": ("pouch", "sand", "signal"),
    "Accessory_EVA_Helmet": ("visor", "bone", "ochre"),
    "Accessory_Pilot_CompactPack": ("case", "bone", "ochre"),
    "Accessory_Civilian_Rucksack": ("pack", "twill", "amber"),
    "Accessory_Utility_Pouch_L": ("pouch", "twill", "amber"),
    "Accessory_Utility_Pouch_R": ("pouch", "twill", "amber"),
    "Accessory_Mining_PowerRig": ("twin", "blueplate", "amber"),
    "Accessory_Research_SampleRack": ("samples", "plum", "ice"),
    "Accessory_Heavy_Helmet": ("helmet", "blueplate", "signal"),
    "Accessory_Shoulder_Heavy_L": ("shoulder", "blueplate", "signal"),
    "Accessory_Shoulder_Heavy_R": ("shoulder", "blueplate", "signal"),
    "Helmet_Guard": ("helmet", "slate", "ice"),
    "Accessory_Mining_Hardhat": ("hardhat", "ochre", "bone"),
    "Accessory_Research_Visor": ("research_visor", "plum", "ice"),
    "Accessory_Goggles": ("goggles", "coal", "amber"),
    "Accessory_Tool_Roll_L": ("pouch", "coal", "amber"),
    "Accessory_Work_Lamp_L": ("lamp", "coal", "ivory"),
    "Accessory_Work_Lamp_R": ("lamp", "coal", "ivory"),
    "Accessory_Mining_Respirator": ("console", "coal", "amber"),
    "Accessory_Medical_Injector_L": ("scanner", "sand", "signal"),
}


def helmet(lo,hi,lod,paint,accent,detail):
    g=Geometry(detail)
    # Open-front crown and jaw ring are joined across the side and back, leaving space for facial fit.
    n=32 if detail else (16,12,8)[lod]
    cx,cy = (lo.x+hi.x)/2,(lo.y+hi.y)/2
    radius_x,radius_y,length=(hi-lo)/2
    z0=lo.z
    for index,(sx,sy,t) in enumerate(((.79,.73,0),(.94,.90,.21),(1,1,.45),(.96,.94,.66),
                                      (.70,.74,.89),(.27,.34,1.0))):
        for i in range(n):
            a=tau*i/n
            g.v.append(Vector((cx+radius_x*sx*cos(a),cy+radius_y*sy*sin(a),z0+length*2*t)))
            g.weights.append({"head":1})
    g.poly(reversed(range(n)),shell("rubber"))
    for row in range(5):
        for i in range(n):
            angle=tau*(i+.5)/n
            opened=sin(angle)<-.39 and 1<=row<=3
            a,b=row*n+i,row*n+(i+1)%n
            finish = GLASS if opened and lod<2 else shell("coal") if opened else shell(
                accent if row>2 and abs(cos(angle))<.50 else paint)
            g.poly((a,b,b+n,a+n),finish,True)
    g.poly(range(5*n,6*n),shell(accent))
    if lod==0:
        for sign in (-1,1):
            g.panel((cx+sign*radius_x*.89,cy,z0+length*.98),
                    (.020,radius_y*.81,length*.55),shell("blueplate"),"head",8)
    return g


def equipment(name, lo, hi, bone, lod, detail=False):
    shape,paint,accent=EQUIPMENT[name]
    if shape in ("visor","helmet"):
        return helmet(lo,hi,lod,paint,accent,detail)
    if lod>=2 and (hi-lo).length<.5 and shape!="hardhat":
        return Geometry()
    g=Geometry(detail)
    x,y,z=(lo+hi)/2
    w,d,h=hi-lo
    back=bone=="back_socket"
    s=1 if back else -1
    n=(8,6,6)[lod]
    base=shell("coal" if shape!="pouch" else paint)
    skin=shell(paint)
    pad=fabric("rubber")
    if shape=="hardhat":
        g.loft([(x,y,z+h*t) for t in (-.5,-.36,-.21,.16,.38,.50)],
               [(w*a,d*b) for a,b in ((.47,.50),(.50,.50),(.48,.45),(.41,.34),(.28,.23),(.10,.10))],
               (16,12,8)[lod],skin,{bone:1},power=.87)
        if lod==0:
            g.panel((x,y-d*.26,z+h*.12),(.35*w,.19*d,.30*h),shell(accent),bone,8)
    elif shape=="goggles":
        for sign in (-1,1):
            cx=x+sign*w*.238
            g.panel((cx,y-d*.13,z),(.445*w,.70*d,.70*h),base,bone,n)
            g.panel((cx,y-d*.45,z),(.350*w,.12*d,.49*h),GLASS,bone,n)
        g.box((x,y-d*.13,z+h*.09),(.17*w,.42*d,.24*h),skin,bone)
    elif shape=="research_visor":
        # A continuous wraparound lens with rearward struts, sized to the original head slot.
        lengths=(11,7)[lod]
        for t,radius in ((-.37,.91),(-.31,1),(.32,1),(.43,.93)):
            for i in range(lengths):
                theta=-.5*pi+pi*i/(lengths-1)
                g.v.append(Vector((x+w*.5*radius*sin(theta),y+d*(.245-.745*cos(theta))*radius,z+h*t)))
                g.weights.append({bone:1})
        for row,finish in enumerate((base,GLASS,skin)):
            for i in range(lengths-1):
                a=row*lengths+i
                g.poly((a,a+1,a+1+lengths,a+lengths),finish,True)
        for sign in (-1,1):
            g.panel((x+sign*w*.44,y+d*.337,z),(.115*w,.326*d,.84*h),skin,bone,6)
    elif shape=="lamp":
        g.panel((x,y,z),(w*.94,d*.95,h*.94),skin,bone,n)
        g.panel((x,y-d*.455,z),(.68*w,.085*d,.68*h),shell(accent),bone,n)
    elif shape=="samples":
        g.panel((x,y-s*d*.19,z),(w*.98,d*.59,h*.96),base,bone,n)
        for sign in (-1,1):
            g.loft([(x+sign*.23*w,y+s*d*.23,z+t*h) for t in (-.40,-.32,.33,.41)],
                   [(w*.17,d*.22)]*4,n,skin,{bone:1})
        if lod==0:
            g.box((x,y+s*d*.413,z+.19*h),(.83*w,.058*d,.075*h),shell(accent),bone)
    elif shape=="pack":
        g.loft([(x,y,z+h*t) for t in (-.48,-.32,.13,.39,.49)],
               [(w*a,d*b) for a,b in ((.30,.31),(.46,.47),(.49,.50),(.38,.44),(.22,.31))],
               10 if lod==0 else 8,fabric(paint),{bone:1},power=.78)
        if lod==0:
            g.panel((x,y+s*d*.410,z-h*.11),(.67*w,.18*d,.32*h),fabric("ochre"),bone,6)
            for sign in (-1,1):
                g.box((x+sign*.22*w,y+s*d*.444,z-h*.025),(.060*w,.07*d,.22*h),pad,bone)
    elif shape in ("twin","case"):
        g.panel((x,y-s*d*.12,z),(w*.96,d*.69,h*.92),base,bone,n)
        if shape=="twin":
            for sign in (-1,1):
                g.panel((x+sign*w*.249,y+s*d*.23,z),(.43*w,.51*d,.88*h),skin,bone,n)
                if lod==0:
                    g.box((x+sign*w*.249,y+s*d*.488,z),(.10*w,.016*d,.56*h),shell(accent),bone)
        else:
            g.panel((x,y+s*d*.21,z),(w*.86,d*.53,h*.79),skin,bone,n)
            if lod==0:
                for sign in (-1,1):
                    g.panel((x+sign*w*.426,y+s*d*.20,z),(.105*w,.57*d,.69*h),pad,bone,6)
        if lod==0:
            if name=="Accessory_Medic_RescuePack":
                g.box((x,y+s*d*.482,z),(.36*w,.038*d,.105*h),shell(accent),bone)
                g.box((x,y+s*d*.482,z+.113*h),(.102*w,.038*d,.113*h),shell(accent),bone)
                g.box((x,y+s*d*.482,z-.113*h),(.102*w,.038*d,.113*h),shell(accent),bone)
            elif name=="Accessory_Combat_Reactor":
                for offset in (-.18,0,.18):
                    g.box((x,y+s*d*.480,z+offset*h),(.47*w,.037*d,.060*h),shell(accent),bone)
            else:
                g.box((x,y+s*d*.45,z+.26*h),(.31*w,.079*d,.075*h),shell(accent),bone)
    elif shape=="rack":
        g.panel((x,y-s*d*.14,z),(.97*w,.68*d,.94*h),base,bone,n)
        if lod==0:
            for cell in (-1,0,1):
                g.panel((x+cell*w*.30,y+s*d*.24,z),(.275*w,.49*d,.72*h),skin,bone,6)
                g.box((x+cell*w*.30,y+s*d*.49,z-h*.29),(.15*w,.015*d,.078*h),shell(accent),bone)
        else:
            g.box((x,y+s*d*.23,z),(.85*w,.50*d,.66*h),skin,bone)
    elif shape=="shoulder":
        g.panel((x,y,z),(w*.97,d*.94,h*.94),skin,bone,n,angle=(.16 if x>0 else -.16))
        if lod==0:
            g.panel((x,y+s*d*.43,z),(.72*w,.12*d,.58*h),shell(accent),bone,6,angle=.2)
    elif shape=="pouch":
        g.panel((x,y,z),(w*.97,d*.94,h*.94),skin,bone,n)
        if lod==0:
            g.panel((x,y+s*.42*d,z+.167*h),(.80*w,.14*d,.47*h),fabric("twill"),bone,6)
            g.box((x,y+s*.49*d,z-.067*h),(.28*w,.02*d,.29*h),shell("coal"),bone)
    else:
        g.panel((x,y,z),(w*.98,d*.95,h*.94),skin,bone,n)
        if lod==0:
            g.panel((x,y+s*.40*d,z),(.68*w,.18*d,.67*h),base,bone,6)
            g.box((x,y+s*.495*d,z),(.46*w,.012*d,.16*h),shell(accent),bone)
    if detail and shape in ("case","plate","console","belt","scanner"):
        radius=min(.006,.055*w,.055*h)
        for dx,dz in ((-.39,-.36),(.39,-.36),(-.39,.36),(.39,.36)):
            g.ellipsoid((x+dx*w,y+s*d*.467,z+dz*h),(radius,.003,radius),
                        shell("coal"),bone,6,3)
    return g
