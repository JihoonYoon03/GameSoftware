"""Anatomical forms and cloth profiles for the reference-sheet character assemblies."""
from collections import defaultdict
from dataclasses import dataclass
from math import atan2, copysign, cos, exp, pi, sin, tau

from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import tessellate_polygon

from human_v4_geometry import Geometry, Surface


SKIN = Surface("Skin", "skin")
HAIR = Surface("Fabric", "hair")
LINER = Surface("Fabric", "black")


def cloth(color):
    return Surface("Fabric", color)


def shell(color):
    return Surface("Shell", color)


def glass(color):
    return Surface("Glass", color)


@dataclass(frozen=True)
class Figure:
    shoulder: float
    rib: float
    waist: float
    hip: float
    chest: float
    back: float
    belly: float
    seat: float
    arm: float
    leg: float
    breast: float = 0.0
    gap: float = 1.0


MALE_BASE = Figure(.232,.195,.161,.187,.108,.101,.104,.134,1.20,1.16)
FEMALE_BASE = Figure(.185,.166,.142,.194,.104,.091,.095,.145,1.06,1.15,.049)


def narrow_figure(base, widths, depth, arm, leg, gap, chest_relief=1.):
    """Scale the transverse profiles of one base figure while keeping its height stations."""
    shoulder,rib,waist,hip=(a*b for a,b in zip((base.shoulder,base.rib,base.waist,base.hip),widths))
    return Figure(shoulder,rib,waist,hip,base.chest*depth,base.back*depth,
                  base.belly*depth,base.seat*depth,base.arm*arm,base.leg*leg,
                  base.breast*chest_relief,gap)


FIGURES = {
    "Male_Lean": narrow_figure(MALE_BASE,(.835,.78,.72,.79),.76,.77,.80,.73),
    "Male_Normal": narrow_figure(MALE_BASE,(.91,.87,.81,.87),.87,.88,.90,.87),
    "Male_Heavy": MALE_BASE,
    "Female_Lean": narrow_figure(FEMALE_BASE,(.865,.80,.75,.80),.77,.80,.82,.74,.65),
    "Female_Normal": narrow_figure(FEMALE_BASE,(.93,.89,.85,.90),.88,.90,.91,.88,.85),
    "Female_Heavy": FEMALE_BASE,
}


def body_base(p):
    return FEMALE_BASE if p.breast else MALE_BASE


def leg_lateral(p,center,radius,upper=False):
    """Reduce the gap between the inner edges as girth changes around each leg center."""
    base=body_base(p)
    if upper:
        center+=(base.hip-.16)*.11
    return radius*p.leg+(center-radius*base.leg)*p.gap


def arm_inset(p):
    return .211*(1-p.shoulder/body_base(p).shoulder)+(.031 if p.breast else 0)


@dataclass(frozen=True)
class Kit:
    sheet: str
    profile: str
    suit: str
    pants: str
    armor: str
    accent: str
    hair: str
    skin: str
    clearance: float


KITS = {
    "Explorer": Kit("Human_Explorer.png","Female_Normal","slate","slate","cream","orange","bun","skin",.016),
    "Guard": Kit("Protective_Service_Guard.png","Male_Normal","carbon","carbon","blue","cyan","crop","tan",.019),
    "Medic": Kit("Field_Medic.png","Female_Normal","sage","earth","cream","coral","bun","skin",.014),
    "Pilot": Kit("Shuttle_Pilot.png","Female_Lean","navy","navy","cream","rust","fringe","warm",.011),
    "Resident": Kit("Port_Resident.png","Female_Normal","ochre","charcoal","khaki","amber","wave","tan",.017),
    "Player_Explorer": Kit("Player_Explorer.png","Male_Normal","slate","slate","cream","orange","wave","tan",.018),
    "Ria_Engineer": Kit("Ria_Engineer.png","Female_Normal","carbon","charcoal","ochre","rust","tied","warm",.018),
    "NPC_Researcher": Kit("NPC_Researcher.png","Male_Normal","dust","dust","teal","lilac","bob","skin",.019),
    "NPC_Miner": Kit("NPC_Miner.png","Male_Heavy","coal","coal","ochre","amber","crop","tan",.025),
    "NPC_HeavyCombat": Kit("NPC_HeavyCombat.png","Male_Heavy","copper","carbon","gunmetal","amber","crop","tan",.032),
}


def choose(stage, edited, close, medium, far):
    return (edited,close,medium,far)[stage+1]


def blend(a,b,t):
    return {a:1-t,b:t} if 0<t<1 else {a:1} if t<=0 else {b:1}


def spine_weight(z):
    return blend("pelvis","spine",(z-.99)/.16) if z<1.15 else blend("spine","chest",(z-1.17)/.16)


def tint_faces(g, start, rings, n, color, bottom=True):
    for r in range(rings-1):
        for i in range(n):
            material=color(r,tau*(i+.5)/n)
            if material:
                g.materials[start+int(bottom)+r*n+i]=material


def torso_levels(p,s,kit):
    # Each ring contains height, frontal half-width, anterior depth and posterior depth.
    levels=[(1.006,p.hip*.99,p.belly*.91,p.seat),(1.056,p.hip*1.01,p.belly,p.seat*.94),
            (1.105,(p.waist+p.hip)*.53,p.belly*.95,p.back*1.01),
            (1.153,p.waist,p.belly*.86,p.back*.91),(1.203,p.waist*1.035,p.chest*.76,p.back*.96),
            (1.250,p.rib*.91,p.chest*.80,p.back),
            (1.292,p.rib*.967,p.chest*.91,p.back*1.06),
            (1.337,p.rib,p.chest,p.back*1.09),(1.374,p.rib*1.02,p.chest*.91,p.back*1.11),
            (1.412,p.shoulder*.90,p.chest*.72,p.back*1.04),
            (1.455,p.shoulder*.75,p.chest*.53,p.back*.83),
            (1.477,p.shoulder*.52,.048,.067),(1.491,.055,.048,.055)]
    select=choose(s,range(13),range(13),(0,1,3,5,7,9,10,11,12),(0,2,4,7,9,10,12))
    levels=[levels[i] for i in select]
    if kit in (KITS["Pilot"],KITS["Resident"]):
        levels=[(z,w+(.008 if 1.02<z<1.16 and kit is KITS["Resident"] else 0),f,b) for z,w,f,b in levels]
    if kit is KITS["Guard"]:
        levels=[(z,w+(.007 if 1.088<z<1.250 else 0),
                 f+(.004 if 1.106<z<1.274 else 0),b+(.004 if 1.250<z<1.420 else 0))
                for z,w,f,b in levels]
    return levels


def torso_sections(s):
    return choose(s,28,24,16,8)


def torso_size(p,s,kit,z):
    levels=torso_levels(p,s,kit)
    for (lo,*a),(hi,*b) in zip(levels,levels[1:]):
        if z<=hi:
            t=max(0,(z-lo)/(hi-lo))
            w,f,back=[x+(y-x)*t for x,y in zip(a,b)]
            return w+kit.clearance,(back-f)*.5,(f+back)*.5+kit.clearance
    _,w,f,back=levels[-1]
    return w+kit.clearance,(back-f)*.5,(f+back)*.5+kit.clearance


def torso(p,s,kit=None):
    g=Geometry()
    extra=kit.clearance if kit else 0
    levels=torso_levels(p,s,kit)
    n=torso_sections(s)
    def contour(co,row,angle):
        if row==0:
            co.z+=.045*abs(cos(angle))**1.5
        if row==len(levels)-1:
            return co
        frontal=max(0,-sin(angle))**1.4
        back=max(0,sin(angle))**1.8
        relief=p.breast*.93 if p.breast else .017
        chest=exp(-((abs(co.x)-p.rib*.44)/.058)**2-((co.z-1.311)/(.049 if p.breast else .056))**2)
        co.y-=frontal*chest*relief
        # Rib cage transitions into the abdominal wall, scapulae and lumbar hollow.
        co.y-=.0075*frontal*exp(-((co.z-1.114)/.080)**2-(co.x/.087)**2)
        co.y+=.009*back*exp(-((co.z-1.383)/.059)**2-((abs(co.x)-.077)/.039)**2)
        co.y-=.007*back*exp(-((co.z-1.161)/.077)**2-(co.x/.074)**2)
        if kit and kit is KITS["Resident"] and s<=0:
            co.y+=(.0035*sin(row*2.29+angle*4)+.0018*cos(angle*7-row))*sin(pi*row/(len(levels)-1))
        return co
    centers=[(0,(b-f)*.5,z) for z,w,f,b in levels]
    radii=[(w+extra,(f+b)*.5+extra) for z,w,f,b in levels]
    surface=cloth(kit.suit if kit else "neutral")
    radii[-1]=(.054+extra*.45,(.048+.055)*.50+extra*.45)
    g.loft(centers,radii,n,surface,[spine_weight(z) for z,*_ in levels],
           power=.84,distortion=contour,caps=False)
    if kit:
        def inlay(row,theta):
            z=levels[row][0]
            side=abs(cos(theta))
            if kit in (KITS["Explorer"],KITS["Player_Explorer"]) and z<1.056:
                return cloth("carbon")
            if kit is KITS["Medic"] and sin(theta)<-.43 and z>1.05:
                return cloth("sand")
            if kit is KITS["Pilot"] and .63<side<.96 and sin(theta)<.12 and z>1.052:
                return cloth("rust")
            if kit is KITS["Guard"] and side>.80:
                return cloth("slate")
            if kit is KITS["Resident"] and (z<1.045 or (z<1.385 and side>.86)):
                return cloth("charcoal")
            if kit is KITS["NPC_Researcher"] and z>1.10 and .15<cos(theta)<.78 and sin(theta)<-.50:
                return cloth("teal")
            return surface
        tint_faces(g,0,len(levels),n,inlay,bottom=False)
    return g


NECK_END=(0,.014,1.564)
NECK_RADII=(.043,.045)
HEAD_SECTIONS=(28,16,12,8)
FACE_ANGLES=(0,45,90,135,180,200,219,235,249,261,270,279,291,305,321,340,360)


def head_angle(stage,angle):
    if stage>0:
        return angle
    sector=angle/tau*16
    i=min(int(sector),15)
    return (FACE_ANGLES[i]+(FACE_ANGLES[i+1]-FACE_ANGLES[i])*(sector-i))*pi/180


def head_section(co,angle,width,depth,center_y):
    co.x=copysign(abs(cos(angle))**.88*width,cos(angle))
    co.y=center_y+copysign(abs(sin(angle))**.88*depth,sin(angle))
    return co


def neck_join(g,offset,n):
    for i in range(offset,offset+n):
        nrm=g.v[i]-Vector(NECK_END)
        nrm.z=-.008
        g.seam_normals[i]=nrm.normalized()


def neck(s,color):
    n=HEAD_SECTIONS[s+1]
    centers=[(0,.009,1.504),(0,.014,1.531),(0,.016,1.548),NECK_END]
    radii=[(.044,.046),(.040,.043),(.041,.044),NECK_RADII]
    def surface(co,row,angle):
        return head_section(co,head_angle(s,angle),*radii[row],centers[row][1])
    g=Geometry().loft(centers,radii,n,Surface("Skin",color),
                      [blend("chest","neck",.59),{"neck":1},{"neck":.58,"head":.42},{"head":1}],
                      power=.88,caps=False,distortion=surface)
    neck_join(g,len(g.v)-n,n)
    return g


def limb(rig,p,s,kind,side,kit=None,open_end=False):
    arm=kind=="Arm"
    primary,secondary=("upper_arm","forearm") if arm else ("thigh","shin")
    first,last=(rig.bones[name+"."+side] for name in (primary,secondary))
    sex="Female" if p.breast else "Male"
    extra=kit.clearance*(.68 if arm else .81) if kit else 0
    direction=1 if side=="L" else -1
    if arm:
        height=[(.270,.054,.056),(.36,.057,.060),(.465,.051,.055),(.63,.044,.049),
                (.84,.036,.041),(1.,.035,.036)]
        lower=[(0,.035,.036),(.17,.044,.043),(.42,.039,.037),(.70,.031,.027),(1,.024,.021)]
        scale=p.arm
    else:
        height=[(.16,.082,.088),(.26,.084,.090),(.38,.078,.086),(.56,.067,.073),
                (.81,.052,.060),(1.,.050,.051)]
        lower=[(0,.050,.051),(.16,.053,.058),(.35,.055,.063),(.66,.039,.045),(1,.030,.032)]
        scale=p.leg
    levels=[(first,a,b,c) for a,b,c in height]+[(last,a,b,c) for a,b,c in lower[1:]]
    selection=choose(s,range(10),range(10),(0,1,3,5,7,8,9),(0,2,5,7,9))
    levels=[levels[i] for i in selection]
    if s==-1:
        extra_rings=((first,.91,.035,.038),(last,.08,.041,.041),(last,.56,.035,.031)) if arm else (
            (first,.92,.050,.056),(last,.075,.051,.055),(last,.50,.050,.056))
        levels.extend(extra_rings)
        levels.sort(key=lambda value:(value[0]==last,value[1]))
    rings=[]
    sizes=[]
    skin=[]
    for bone,t,x,y in levels:
        start,end=bone.head_local,bone.tail_local
        co=start.lerp(end,t)
        if arm:
            reduction=(1-.44*t) if bone==first else .56*(1-t)
            co.x-=direction*arm_inset(p)*reduction
            if bone==first:
                co.x+=direction*(.211-abs(start.x))*(1-t)
                co.y+=.003*(1-t)
        else:
            upper=bone==first
            lateral=leg_lateral(p,abs(co.x),x,upper)
            co.x=direction*max(lateral,x*p.leg+extra+.002)
            co.y+=.021*(1-t)**2 if upper else .015*sin(pi*t)
        rings.append(co)
        tailoring=1
        if kit is KITS["Guard"]:
            full=.049 if arm else .063
            taper=.018 if arm else .028
            tailoring+=full*sin(pi*t) if bone==first else -taper*sin(pi*t)
        sizes.append((x*scale*tailoring+extra,y*scale*tailoring+extra))
        if bone==first:
            if t<.18:
                parent="clavicle."+side if arm else "pelvis"
                skin.append(blend(parent,first.name,.31+max(0,t)*3))
            else:
                skin.append(blend(first.name,last.name,max(0,(t-.63)*.5/.37)))
        else:
            ending=("hand." if arm else "foot.")+side
            skin.append(blend(first.name,last.name,.50+t*1.9) if t<.27 else
                        blend(last.name,ending,max(0,(t-.92)*3)))
    g=Geometry()
    n=choose(s,22,16,10,6)
    surface=cloth(kit.suit if arm else kit.pants) if kit else SKIN
    def folds(co,row,angle):
        if kit and s<=0:
            bone,t,*_=levels[row]
            # Pleats have volume and fade out before each attachment edge.
            soft=kit in (KITS["Resident"],KITS["Ria_Engineer"],KITS["NPC_Researcher"],KITS["Medic"])
            crease=(.82 if bone==first else .12)
            falloff=exp(-((t-crease)/.20)**2)
            co+=(co-rings[row]).normalized()*(.0085 if soft else .0060)*falloff*(
                .44*sin(4*angle+row*1.31)+.56*cos(angle*2-row*2.29))
        return co
    axis=last.tail_local-first.head_local
    g.loft(rings,sizes,n,surface,skin,axis=axis,power=.91,distortion=folds,caps=(False,not open_end))
    g.distal_ring=range((len(rings)-1)*n,len(rings)*n)
    def color(row,angle):
        if not kit:
            if not arm and sex=="Male" and row<(2 if s==2 else 4):
                return cloth("neutral")
        elif kit is KITS["Pilot"] and abs(cos(angle))>.83 and cos(angle)*direction>0:
            return cloth("rust")
        elif kit is KITS["Resident"] and arm and (sin(angle)>.63 or (row in (3,4,5) and abs(cos(angle))>.36)):
            return cloth("charcoal")
        elif kit is KITS["Ria_Engineer"] and arm and cos(angle)*direction>.62:
            return cloth("rust" if row<4 or row>6 else "sand")
        elif kit is KITS["Guard"] and cos(angle)*direction>.66:
            return cloth("slate")
        elif kit is KITS["NPC_Miner"] and ((cos(angle)*direction>.42 and not arm) or (arm and 2<=row<=5)):
            return cloth("ochre")
        return None
    tint_faces(g,0,len(rings),n,color,bottom=False)
    return g


def stitch(g,loop,count,material,axis,pattern=None):
    """Join an anatomical boundary and a limb ring, preserving the boundary weights."""
    points=[p for p,w in loop]
    start_index=len(g.v)
    if pattern and "order" in pattern:
        for i in pattern["order"]:
            co,weights=loop[i]
            g.v.append(co.copy())
            g.weights.append(weights)
        for face in pattern["faces"]:
            g.poly(face,material,True)
        return
    center=sum(points,Vector())/len(points)
    ring_center=sum(g.v[:count],Vector())/count
    axial=Vector(axis).normalized()
    travel=(ring_center-center).normalized()
    projection=(axial+travel*.73).normalized()
    area=sum((a-center).cross(b-center).dot(projection) for a,b in zip(points,points[1:]+points[:1]))
    order=list(range(len(loop)))
    if area<0:
        order.reverse()
    start=g.v[0]-ring_center
    front=(start-projection*projection.dot(start)).normalized()
    up=projection.cross(front)
    def phase(co,origin):
        delta=co-origin
        return atan2(delta.dot(up),delta.dot(front))
    first=min(range(len(order)),key=lambda i:abs(phase(loop[order[i]][0],center)))
    order=order[first:]+order[:first]
    loop=[loop[i] for i in order]
    for coord,weights in loop:
        g.v.append(coord.copy())
        g.weights.append(weights)
    m=len(loop)
    seam_origin=phase(loop[0][0],center)
    def progress(items,origin,shift):
        values=[0.]
        for co in items[1:]:
            a=(phase(co,origin)-shift)%tau/tau
            if a <= values[-1]+1e-5:
                # Highly concave junctions are already in perimeter order.
                return [i/len(items) for i in range(len(items)+1)]
            values.append(a)
        return values+[1.]
    near_steps=progress([co for co,weights in loop],center,seam_origin)
    far_steps=progress(g.v[:count],ring_center,0)
    first_face=len(g.faces)
    i=j=0
    while i<m or j<count:
        a=start_index+i%m
        b=j%count
        near=near_steps[i+1] if i<m else 2
        far=far_steps[j+1] if j<count else 2
        if abs(near-far)<.067 and i<m and j<count:
            g.poly((a,start_index+(i+1)%m,(j+1)%count,b),material,True)
            i+=1
            j+=1
        elif near<far:
            g.poly((a,start_index+(i+1)%m,b),material,True)
            i+=1
        else:
            g.poly((a,(j+1)%count,b),material,True)
            j+=1
    if pattern is not None:
        pattern.update(order=order,faces=g.faces[first_face:])


def smooth_joins(geometries):
    candidates=defaultdict(list)
    for g in geometries:
        for i,co in enumerate(g.v):
            candidates[tuple(round(x,6) for x in co)].append((g,i))
    shared=[entries for entries in candidates.values() if len({id(g) for g,_ in entries})>1]
    normals={g:[Vector() for v in g.v] for g in geometries}
    for g in geometries:
        for face,smooth in zip(g.faces,g.smooth):
            if smooth:
                coords=[g.v[i] for i in face]
                normal=sum((a.cross(b) for a,b in zip(coords,coords[1:]+coords[:1])),Vector())
                for i in face:
                    normals[g][i]+=normal
    for entries in shared:
        direction=sum((normals[g][i] for g,i in entries),Vector()).normalized()
        if direction.length_squared:
            for g,i in entries:
                g.seam_normals[i]=direction


def fitted_body(rig,p,s,kit,color,*,open_ends=False,patterns=None):
    body=torso(p,s,kit)
    top=neck(s,color)
    arms={side:limb(rig,p,s,"Arm",side,kit,open_ends) for side in ("L","R")}
    legs={side:limb(rig,p,s,"Leg",side,kit,open_ends) for side in ("L","R")}
    def profile(slot):
        return patterns.setdefault(slot,{}) if patterns is not None else None
    n=torso_sections(s)
    limb_n=choose(s,22,16,10,6)
    levels=torso_levels(p,s,kit)
    armpit=next(i for i,(z,*_) in enumerate(levels) if z>1.335)
    shoulder=next(i for i,(z,*_) in enumerate(levels) if z>1.450)
    span=2 if n>=16 else 1
    hidden=set()
    def sample(ring,column):
        i=ring*n+column%n
        return body.v[i],body.weights[i]
    for side,mid in (("L",0),("R",n//2)):
        a,b=mid-span,mid+span
        arm_name="upper_arm."+side
        for row in range(armpit,shoulder+1):
            opacity=.16 if row==armpit else .28 if row==shoulder else .39
            for column in range(a,b+1):
                i=row*n+column%n
                body.weights[i]=blend("chest",arm_name,opacity*(1-.30*abs(column-mid)/span))
        loop=[sample(armpit,j) for j in range(a,b+1)]
        loop.extend(sample(i,b) for i in range(armpit+1,shoulder+1))
        loop.extend(sample(shoulder,j) for j in reversed(range(a,b)))
        loop.extend(sample(i,a) for i in reversed(range(armpit+1,shoulder)))
        hidden.update(i*n+j%n for i in range(armpit,shoulder) for j in range(a,b))
        bone=rig.bones["upper_arm."+side]
        stitch(arms[side],loop,limb_n,cloth(kit.suit) if kit else SKIN,
               bone.tail_local-bone.head_local,profile("shoulder_"+side))
    for index in sorted(hidden,reverse=True):
        for values in (body.faces,body.materials,body.smooth):
            del values[index]

    front,back=n*3//4,n//4
    front_coord,back_coord=body.v[front],body.v[back]
    saddle=[]
    for f,z in ((.23,.955),(.48,.922),(.75,.946)):
        coord=front_coord.lerp(back_coord,f)
        coord.z=z
        saddle.append((coord,{"pelvis":1}))
    for side,start,end,inside in (
            ("L",front,back+n,list(reversed(saddle))),("R",back,front,saddle)):
        loop=[sample(0,j) for j in range(start,end+1)]+inside
        mat=cloth(kit.pants) if kit else cloth("neutral")
        stitch(legs[side],loop,limb_n,mat,(0,0,-1),profile("hip_"+side))

    # The neckline bridge uses the reverse direction: from the upper ring into the body.
    ring=[sample(len(levels)-1,j) for j in range(n)]
    stitch(top,ring,HEAD_SECTIONS[s+1],Surface("Skin",color),(0,0,1),profile("neck"))
    active=sorted({i for face in body.faces for i in face})
    remap={old:new for new,old in enumerate(active)}
    body.v=[body.v[i] for i in active]
    body.weights=[body.weights[i] for i in active]
    body.faces=[tuple(remap[i] for i in face) for face in body.faces]
    smooth_joins((body,top,*arms.values(),*legs.values()))
    return body,top,arms,legs


def feet(s,side,color,p):
    x=leg_lateral(p,.115,.030)*(1 if side=="L" else -1)
    n=choose(s,20,14,10,8)
    width=.73+.27*p.leg/body_base(p).leg
    return Geometry().loft([(x,-.055,.003),(x,-.057,.030),(x,-.045,.065),(x,-.008,.104),(x,.001,.150)],
                           [(a*width,b) for a,b in ((.051,.104),(.054,.105),(.051,.095),(.037,.066),(.031,.034))],
                           n,Surface("Skin",color),{"foot."+side:1},power=.84)


def boots(s,side,kit,p):
    g=Geometry()
    x=leg_lateral(p,.115,.030)*(1 if side=="L" else -1)
    n=choose(s,22,16,12,8)
    bone="foot."+side
    dark=kit in (KITS["Guard"],KITS["NPC_Miner"],KITS["NPC_HeavyCombat"])
    light="gunmetal" if dark else "stone"
    fabric="carbon" if dark else "brown"
    levels=[(.002,-.065,.061,.123),(.014,-.069,.065,.130),(.039,-.067,.064,.127),
            (.062,-.063,.060,.116),(.089,-.055,.057,.101),(.114,-.035,.051,.082),
            (.147,-.017,.047,.063),(.169,-.008,.053,.059),(.202,-.005,.053,.057)]
    select=choose(s,range(9),range(9),(0,2,3,5,7,8),(0,2,3,5,7,8))
    rows=[levels[i] for i in select]
    width=.73+.27*p.leg/body_base(p).leg
    g.loft([(x,y,z) for z,y,a,b in rows],[(a*width,b) for z,y,a,b in rows],n,cloth(fabric),
           [blend(bone,"shin."+side,max(0,(z-.123)*7.8)) for z,*_ in rows],power=.69)
    def panels(index,theta):
        z=rows[index][0]
        if z<.035:
            return LINER
        if z<.062 or z>=.147:
            return shell(light)
        if sin(theta)<-.70:
            return shell(light) if z<.089 else cloth("charcoal")
        return cloth(fabric)
    tint_faces(g,0,len(rows),n,panels)
    if s<=0:
        for z,y in ((.092,-.156),(.114,-.118),(.139,-.092)):
            ribbon(g,[(x-.031*width,y+.009,z),(x,y,z),(x+.031*width,y+.009,z)],.009,cloth("charcoal"),bone)
    return g


def hands(s,side,color,glove=False):
    g=Geometry()
    sign=1 if side=="L" else -1
    bone="hand."+side
    n=choose(s,14,10,8,8)
    mat=cloth("carbon") if glove else Surface("Skin",color)
    g.loft([(sign*.461,-.019,.851),(sign*.449,-.018,.887),(sign*.438,-.017,.916),(sign*.428,-.015,.950)],
           [(.026,.018),(.035,.025),(.030,.023),(.026,.023)],n,mat,{bone:1},power=.87)
    if s<=0:
        finger=choose(s,10,6,6,6)
        for k in range(4):
            x=sign*.460+(k-1.5)*.014
            tip=.815+abs(k-1.3)*.007
            g.loft([(x+sign*.005,-.033,tip),(x+sign*.009,-.030,tip+.012),(x,-.022,.864)],
                   [(.0048,.0055),(.0064,.0063),(.0072,.0075)],finger,mat,{bone:1})
        g.loft([(sign*.416,-.046,.853),(sign*.417,-.045,.871),(sign*.438,-.023,.901)],
               [(.0077,.0076),(.011,.010),(.012,.012)],finger,mat,{bone:1})
    return g


def plate(g,outline,y,depth,front,edge,bone,back=False,curve=0.,follow=None):
    """Beveled face with an authored outline, curved over the supporting equipment."""
    xmid=sum(x for x,z in outline)/len(outline)
    zmid=sum(z for x,z in outline)/len(outline)
    sign=1 if back else -1
    start=len(g.v)
    points=list(outline)
    # Work around the outer edge counterclockwise in the displayed X/Z plane.
    if sum(a*d-c*b for (a,b),(c,d) in zip(points,points[1:]+points[:1]))<0:
        points.reverse()
    if back:
        points.reverse()
    n=len(points)
    def position(x,z,f):
        surface=follow(x,z) if follow is not None else -sign*curve*(x-xmid)**2
        return (x,y+surface+sign*depth*f,z)
    for f,k in ((0,.91),(.18,1),(.77,1),(1,.91)):
        for x,z in points:
            px=xmid+(x-xmid)*k
            pz=zmid+(z-zmid)*k
            g.v.append(Vector(position(px,pz,f)))
            g.weights.append({bone:1} if isinstance(bone,str) else bone)
    tessellation=tessellate_polygon([[Vector((x,z,0)) for x,z in points]])
    for triangle in tessellation:
        g.poly(tuple(start+i for i in reversed(triangle)),edge)
    for j in range(3):
        for i in range(n):
            a,b=start+j*n+i,start+j*n+(i+1)%n
            g.poly((a,b,b+n,a+n),edge if j==0 else front)
    # Side walls and the visible bevel remain separate from the face for sharp panel normals.
    if follow is None:
        for triangle in tessellation:
            g.poly(tuple(start+3*n+i for i in triangle),front)
    else:
        center=len(g.v)
        g.v.append(Vector(position(xmid,zmid,1)))
        g.weights.append({bone:1} if isinstance(bone,str) else bone)
        for i in range(n):
            g.poly((start+3*n+i,start+3*n+(i+1)%n,center),front,True)
    return g


OCT=((-1,-.63),(-.72,-1),(.72,-1),(1,-.63),(1,.65),(.68,1),(-.68,1),(-1,.65))


def panel(g,x,y,z,w,h,d,mat,edge,bone,back=False,obl=OCT,arc=0.,follow=None):
    return plate(g,[(x+u*w*.5,z+v*h*.5) for u,v in obl],y,d,mat,edge,bone,back,arc,follow)


def ribbon(g,path,width,mat,bone,axis=(0,1,0),follow=None):
    """A trim or strap strip following an already-shaped surface."""
    start=len(g.v)
    points=[Vector(p) for p in path]
    normal=Vector(axis)
    for i,p in enumerate(points):
        tangent=points[min(i+1,len(points)-1)]-points[max(i-1,0)]
        cross=tangent.cross(normal).normalized()*width*.5
        for sign in (-1,1):
            coord=p+cross*sign
            if follow:
                coord.y=follow(coord.x,coord.z)-.002
            g.v.append(coord)
            g.weights.append({bone:1} if isinstance(bone,str) else bone)
    for i in range(len(points)-1):
        a=start+i*2
        face=(a,a+2,a+3,a+1)
        g.poly(face,mat,True)
    return g


def collar(s,p,kit):
    g=Geometry()
    n=choose(s,22,16,10,8)
    dark=kit in (KITS["Guard"],KITS["NPC_Miner"],KITS["NPC_HeavyCombat"],KITS["Ria_Engineer"],KITS["NPC_Researcher"])
    mat=shell("gunmetal" if dark else "ochre" if kit is KITS["Resident"] else "cream")
    g.loft([(0,.003,1.463),(0,.005,1.483),(0,.007,1.518),(0,.009,1.530)],
           [(.090,.087),(.079,.079),(.071,.070),(.067,.066)],
           n,mat,blend("chest","neck",.36),power=.83,caps=False)
    if s<2:
        g.loft([(0,.008,1.524),(0,.009,1.534)],[(.068,.067),(.064,.063)],n,LINER,
               blend("chest","neck",.36),caps=False)
        if kit in (KITS["Explorer"],KITS["Player_Explorer"],KITS["Pilot"]):
            panel(g,0,-.067,1.492,.057,.038,.022,shell("gunmetal"),LINER,"neck")
            ribbon(g,[(-.014,-.090,1.493),(.014,-.090,1.493)],.007,shell("amber"),"neck")
    return g


def head(s,p,color):
    g=Geometry()
    female=p.breast>0
    w=.077 if female else .083
    c=Surface("Skin",color)
    n=HEAD_SECTIONS[s+1]
    # Jaw, lower lip, nose base, bridge, orbits, brow and the vault of the skull.
    rings=[(1.564,*NECK_RADII,NECK_END[1]),(1.579,w*(.56 if female else .65),.054,.014),
           (1.590,w*(.65 if female else .73),.068,.008),(1.602,w*.76,.069,.009),
           (1.615,w*.81,.073,.008),(1.627,w*.87,.073,.007),(1.641,w*.91,.073,.008),
           (1.653,w*.96,.074,.009),(1.674,w*.97,.073,.012),(1.692,w*.98,.078,.014),
           (1.707,w,.084,.016),(1.727,w,.084,.016),(1.748,w*.92,.080,.017),
           (1.772,w*.74,.070,.019),(1.793,w*.41,.041,.019),(1.803,.006,.007,.019)]
    select=choose(s,(0,1,2,4,6,7,8,9,11,12,13,14,15),(0,1,2,4,6,7,8,9,11,12,13,15),
                  (0,1,3,4,7,8,10,13,15),(0,1,5,8,10,13,15))
    levels=[rings[i] for i in select]
    def sculpt(co,i,angle):
        angle=head_angle(s,angle)
        if s<=0:
            _,width,depth,center_y=levels[i]
            head_section(co,angle,width,depth,center_y)
        front=max(0,-sin(angle))**1.7
        if front and i>0:
            x,z=co.x,co.z
            nose=exp(-(x/.012)**2-((z-1.644)/.009)**2)
            bridge=exp(-(x/.013)**2-((z-1.661)/.020)**2)
            orbit=exp(-((abs(x)-.032)/.018)**2-((z-1.674)/.012)**2)
            cheekbone=exp(-((abs(x)-.050)/.019)**2-((z-1.651)/.013)**2)
            lower_cheek=exp(-((abs(x)-.042)/.021)**2-((z-1.621)/.017)**2)
            chin=exp(-(x/(.024 if female else .029))**2-((z-1.590)/.009)**2)
            lip=exp(-(x/.025)**2-((z-1.616)/.008)**2)
            brow=exp(-((abs(x)-.031)/.029)**2-((z-1.691)/.010)**2)
            nose_wings=exp(-((abs(x)-.013)/.007)**2-((z-1.640)/.006)**2)
            co.y+=front*(-.027*nose-.019*bridge-.009*nose_wings+.010*orbit-.010*cheekbone
                         +.007*lower_cheek-.009*chin-.006*lip-(.004 if female else .007)*brow)
        return co
    g.loft([NECK_END,*[(0,y,z) for z,a,b,y in levels[1:]]],
           [NECK_RADII,*[(a,b) for z,a,b,y in levels[1:]]],n,c,{"head":1},
           power=.88,distortion=sculpt,caps=(False,True))
    neck_join(g,0,n)
    if s<0:
        for sign in (-1,1):
            g.ellipsoid((sign*w*.960,.023,1.658),(.013,.012,.024),c,"head",8,4)
    if s==0:
        for sign in (-1,1):
            g.ellipsoid((sign*w*.965,.023,1.660),(.014,.012,.023),c,"head",5,2)
    return g


def features(s,style,base_head,color):
    g=Geometry()
    face=BVHTree.FromPolygons(base_head.v,base_head.faces)
    def skin_y(x,z):
        hit=face.ray_cast(Vector((x,-1,z)),Vector((0,1,0)))[0]
        return hit.y if hit is not None else -.067

    def patch(loop,center,finish,lift):
        start=len(g.v)
        for x,z in [*loop,center]:
            g.v.append(Vector((x,skin_y(x,z)-lift,z)))
            g.weights.append({"head":1})
        for i in range(len(loop)):
            g.poly((start+i,start+(i+1)%len(loop),start+len(loop)),finish,True)

    def eyelid(path,width,finish):
        return ribbon(g,[(x,0,z) for x,z in path],width,finish,"head",follow=skin_y)

    eye=Surface("Skin","eye")
    iris=Surface("Skin","iris")
    pupil=Surface("Skin","ink")
    rim=Surface("Skin","lid_"+color)
    lip=Surface("Skin","lip_"+color)
    if s>=2:
        for sign in (-1,1):
            x=sign*.0325
            patch([(x-.010,1.674),(x,1.672),(x+.010,1.674),(x,1.677)],(x,1.674),rim,.0023)
            patch([(x-.010,1.690),(x,1.689),(x+.010,1.690),(x,1.692)],(x,1.690),rim,.0023)
        patch([(-.014,1.615),(0,1.612),(.014,1.615),(0,1.618)],(0,1.615),lip,.0022)
        return g
    for sign in (-1,1):
        x=sign*.0325
        z=1.674
        half=.0133
        outline=[(x-half,z-.0006),(x-.006,z-.0031),(x+.006,z-.0025),(x+half,z+.0005),
                 (x+.006,z+.0031),(x,z+.0043),(x-.007,z+.0034)]
        patch(outline,(x,z),eye,.0018)
        px=x-sign*.0009
        patch([(px+.0047*cos(tau*i/8),z+.0038*sin(tau*i/8)) for i in range(8)],(px,z),iris,.0026)
        if s<=0:
            patch([(px+.0021*cos(tau*i/6),z+.0021*sin(tau*i/6)) for i in range(6)],(px,z),pupil,.0032)
            eyelid([outline[0],*reversed(outline[3:])],.00155,rim)
        eyelid([(x-.015,1.691),(x-.003,1.695),(x+.007,1.694),(x+.015,1.690)],
               .0025 if style!="crop" else .00325,HAIR)
        if s<=0:
            cx=sign*.0105
            patch([(cx-.0022,1.6399),(cx,1.6390),(cx+.0025,1.6401),(cx,1.6412)],
                  (cx,1.6401),rim,.0025)
    patch([(-.020,1.6155),(-.013,1.6132),(-.0065,1.6115),(0,1.6111),(.0065,1.6115),
           (.013,1.6132),(.020,1.6155),(.012,1.6173),(.0055,1.6193),(0,1.6178),
           (-.0055,1.6193),(-.012,1.6173)],(0,1.6155),lip,.003)
    if s<=0:
        line=[(-.019,1.6155),(-.011,1.6155),(-.0055,1.6161),(0,1.6152),
              (.0055,1.6161),(.011,1.6155),(.019,1.6155)]
        ribbon(g,[(x,0,z) for x,z in line],.0011,rim,"head",
               follow=lambda x,z:skin_y(x,z)-.0018)
    return g


def hair_uv(tile,u,v):
    row,col=divmod(tile,4)
    return ((col+(10.5+235*u)/256)*.25,1-(row+(10.5+491*v)/512)*.5)


def hair_card(g,rows,tile):
    start=len(g.v)
    for i,(a,b) in enumerate(rows):
        t=i/(len(rows)-1)
        for j,co in enumerate((a,b)):
            vertex=len(g.v)
            g.v.append(Vector(co))
            g.weights.append({"head":1})
            g.vertex_uv[vertex]=hair_uv(tile,j,t)
    for i in range(len(rows)-1):
        a=start+i*2
        g.poly((a,a+1,a+3,a+2),Surface("HairCards","strand"),True)


def hair(s,style):
    g=Geometry()
    n=choose(s,24,14,12,9)
    levels=[(1.652,.080,.080),(1.695,.085,.086),(1.736,.086,.090),(1.771,.073,.079),
            (1.799,.049,.054),(1.820,.016,.021)]
    if style=="crop":
        levels=[(1.669,.086,.080),(1.702,.088,.086),(1.737,.085,.087),(1.768,.076,.077),
                (1.795,.053,.057),(1.810,.015,.019)]
    elif style=="fringe":
        levels=[(z-.005,w-.002,d-.001) for z,w,d in levels]
    elif style=="bob":
        levels=[(1.623,.082,.079),*levels[1:]]
    def line(co,row,angle):
        front=max(0,-sin(angle))
        if row<5:
            ascent=(.075,.058,.043,.021,.004) if style=="crop" else (.071,.049,.033,.014,.003)
            co.z+=ascent[row]*front
        if row==0:
            co.z+=sin(angle*2+.51)*(.012 if style=="wave" else .0055)
            if style=="fringe":
                co.z-=.024*front*max(0,cos(angle+.42)+.29)
            if style=="bob":
                co.z+=.038*front*max(0,abs(sin(angle))-.15)
        elif row<5 and style=="wave":
            co.y-=.006*cos(angle*3+.45)
            co.z+=.007*sin(angle*4-.31)
        return co
    g.loft([(0,.019,z) for z,a,b in levels],[(a,b) for z,a,b in levels],n,HAIR,
           {"head":1},distortion=line,caps=(False,True))
    if style=="bun":
        g.ellipsoid((-.012,.091,1.773),(.043,.043,.043),HAIR,"head",
                    choose(s,14,9,8,7),choose(s,7,4,4,4))
    if style=="tied":
        g.loft([(-.012,.151,1.575),(-.006,.162,1.619),(-.007,.151,1.679),(-.014,.093,1.742)],
               [(.007,.009),(.018,.015),(.030,.026),(.025,.022)],choose(s,15,10,7,6),
               HAIR,{"head":1},power=.74)
    if s<=0:
        for i,co in enumerate(g.v):
            if i<n*len(levels):
                u,v=co.x*4.5,(co.y-.019)*4
            else:
                u=co.x*4.5+(co.y-.091)*1.10
                v=(1.670-co.z)*4.65+(co.y-.140)*.60 if style=="tied" else (1.773-co.z)*8+(co.y-.091)*.88
            g.vertex_uv[i]=hair_uv(7,.5+u,.5+v)
        g.materials=[Surface("HairCards","strand")]*len(g.materials)
        count=choose(s,28,16,0,0)
        def scalp(ring,theta,rise):
            k=(theta%tau)*n/tau
            i=int(k)
            low=int(ring)
            high=min(low+1,5)
            a=g.v[low*n+i%n].lerp(g.v[low*n+(i+1)%n],k-i)
            b=g.v[high*n+i%n].lerp(g.v[high*n+(i+1)%n],k-i)
            p=a.lerp(b,ring-low)
            radial=Vector((p.x,p.y-.019,(p.z-1.744)*1.3)).normalized()
            return p+radial*rise
        for i in range(count):
            angle=tau*(i+.30)/count
            half=.27 if style=="crop" else .36
            sweep=.41 if style in ("fringe","wave") else .24
            samples=(4.64,3.35,2.14,1.03,.01)
            rows=[]
            for step,ring in enumerate(samples):
                t=step/(len(samples)-1)
                center=angle+(t-.5)*sweep+sin(angle*3)*.093*t
                width=half*(.52+.64*sin(pi*t*.86))
                rise=.0013+.0026*sin(pi*t)
                rows.append((scalp(ring,center-width,rise),scalp(ring,center+width,rise)))
            hair_card(g,rows,i%7)
        if style in ("bun","wave","fringe","tied","bob"):
            for side in (-1,1):
                x=side*(.064 if style=="bun" else .046)
                z_meridian=1.705 if style=="bob" else 1.728 if style in ("bun","tied") else 1.744
                paths=[(x*.48,-.059,1.789),(x*.76,-.074,1.766),(x*.95,-.072,z_meridian),
                       (x*1.11,-.053,z_meridian-.027),(x*1.06,-.041,z_meridian-.043)]
                widths=(.013,.013,.011,.006,.004) if style=="bun" else (.019,.019,.016,.010,.006)
                rows=[((a-w,b,c),(a+w,b,c+.001)) for (a,b,c),w in zip(paths,widths)]
                hair_card(g,rows,1 if side==-1 else 5)
        if style=="bun":
            for i in range(5):
                x=(i-2)*.014-.012
                rows=[((x-.010,.092,1.810),(x+.010,.092,1.810)),
                      ((x-.010,.118,1.800),(x+.010,.120,1.799)),
                      ((x-.010,.132,1.773),(x+.010,.134,1.773)),
                      ((x-.008,.121,1.750),(x+.008,.123,1.749))]
                hair_card(g,rows,(i+2)%7)
    return g
