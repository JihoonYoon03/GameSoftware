"""Wearable panels and detachable equipment interpreted from the drawing callouts."""
from math import copysign, cos, pi, sin, tau

from mathutils import Vector
from mathutils.bvhtree import BVHTree

from human_equipment_atlas import tile_uv
from human_guard_armor import MARKED
from human_sheet_forms import (
    Geometry, KITS, LINER, OCT, arm_inset, blend, body_base, choose, cloth, collar, glass, leg_lateral, panel,
    plate, ribbon, shell, spine_weight, torso_size,
)

HEX_BEVEL=((-1,-.64),(-.69,-1),(.69,-1),(1,-.64),(1,.73),(-1,.73))
RECT=((-1,-1),(1,-1),(1,1),(-1,1))


def block(g, p, size, color, bone):
    return g.box(p,size,shell(color),bone)


def front_datum(p, kit, z):
    if z>=1.33:
        depth=p.chest+p.breast*.53
    elif z>=1.18:
        depth=p.chest*.80+p.breast*.34
    else:
        depth=p.belly
    return -depth-kit.clearance


def trace_front(g,fallback):
    bvh=BVHTree.FromPolygons(g.v,g.faces)
    def cast(x,z):
        hit=bvh.ray_cast(Vector((x,-1,z)),Vector((0,1,0)))[0]
        return hit.y if hit is not None else fallback(x,z)
    return cast


def mark_face(g, tile, outline_count, *, follows_surface=False, back=False):
    """Lay paint and shallow stamp detail on a plate's existing outer face."""
    count=outline_count if follows_surface else outline_count-2
    mark_faces(g,tile,range(len(g.faces)-count,len(g.faces)),back=back)


def mark_faces(g,tile,indices,*,back=False):
    points=[g.v[i] for face in indices for i in g.faces[face]]
    left,right=min(v.x for v in points),max(v.x for v in points)
    bottom,top=min(v.z for v in points),max(v.z for v in points)
    for face in indices:
        g.materials[face]=MARKED
        coordinates=[((g.v[i].x-left)/(right-left),(g.v[i].z-bottom)/(top-bottom)) for i in g.faces[face]]
        g.face_uv[face]=tuple(tile_uv(tile,1-u if back else u,v) for u,v in coordinates)


def fittings(g, s, xs, y, z, bone, mat):
    if s<=0:
        for x in xs:
            g.box((x,y,z),(.019,.009,.022),shell(mat),bone)


def harness(p,s,kit):
    g=Geometry()
    if s>=2 and kit not in (KITS["Pilot"],KITS["Explorer"]):
        return g
    if kit in (KITS["Guard"],KITS["Pilot"]):
        mat=shell("gunmetal" if kit is KITS["Guard"] else "cream")
    else:
        mat=cloth("charcoal")
    x=p.shoulder*.64
    for side in (-1,1):
        path=[(x*side,p.back+.041,1.355),(x*side,p.back+.015,1.423),
              (side*x*.88,.011,1.477),(side*x*.96,-.061,1.440),
              (side*x*1.06,front_datum(p,kit,1.33)*.88,1.359),
              (side*x*.86,front_datum(p,kit,1.28)-.015,1.238)]
        width=.035 if kit in (KITS["Medic"],KITS["Resident"]) else .031
        # The strip switches its width plane from the shoulders to the chest.
        ribbon(g,path[:5],width,mat,"chest",axis=(0,.78,-.63))
        if kit is not KITS["Pilot"]:
            ribbon(g,path[4:],width,mat,"chest")
        if s<=0:
            fittings(g,s,[side*x*.97],-.085,1.424,"chest",
                     "cyan" if kit is KITS["Guard"] else "metal")
            if kit is not KITS["Pilot"]:
                fittings(g,s,[side*x*1.04],path[4][1]-.014,1.359,"chest","metal")
    return g


def chest_shell(p,s,kit,body):
    g=Geometry()
    w=p.rib
    y=front_datum(p,kit,1.36)
    surface=trace_front(body,lambda x,z:front_datum(p,kit,z)+.043)
    if kit is KITS["Explorer"]:
        outline=[(-.46,1.205),(.46,1.205),(.74,1.246),(.78,1.306),
                 (.96,1.393),(.71,1.428),(.49,1.422),(.31,1.400),
                 (-.31,1.400),(-.49,1.422),(-.71,1.428),(-.96,1.393),(-.78,1.306),(-.74,1.246)]
        plate(g,[(u*w,z) for u,z in outline],.004,.048,shell("cream"),LINER,"chest",follow=surface)
        mark_face(g,"explorer",len(outline),follows_surface=True)
        if s<2:
            plate(g,[(-.051,1.194),(.050,1.194),(.062,1.233),(-.062,1.233)],
                  .002,.030,shell("stone"),LINER,spine_weight(1.23),follow=surface)
    elif kit is KITS["Guard"]:
        outline=[(-.94,1.296),(-.62,1.269),(-.28,1.288),(.28,1.288),(.62,1.269),(.94,1.296),
                 (1.10,1.367),(.92,1.441),(.56,1.447),(.31,1.422),(-.31,1.422),(-.56,1.447),(-.92,1.441),(-1.10,1.367)]
        plate(g,[(u*w,z) for u,z in outline],-.004,.054,shell("blue"),LINER,"chest",follow=surface)
        rows=3 if s<2 else 1
        for i in range(rows):
            z=1.239-i*.063
            mat="blue" if i!=1 else "darkblue"
            panel(g,0,.000,z,w*(1.63-i*.19),.068,.039,
                  shell(mat),LINER,spine_weight(z),obl=RECT if s>=2 else OCT,follow=surface)
            if s<=0:
                slot_y=trace_front(g,surface)(0,z+.018)
                block(g,(0,slot_y-.004,z+.018),(.045,.010,.013),"gunmetal","spine")
        if s<2:
            module_y=trace_front(g,surface)(.062,1.395)
            block(g,(.062,module_y-.005,1.395),(.051,.010,.022),"gunmetal","chest")
            block(g,(.062,module_y-.011,1.395),(.038,.004,.013),"cyan","chest")
    elif kit is KITS["Medic"]:
        outline=[(-.70,1.286),(.70,1.286),(.92,1.360),(.70,1.430),(.44,1.439),
                 (.28,1.417),(-.28,1.417),(-.44,1.439),(-.70,1.430),(-.92,1.360)]
        plate(g,[(u*w,z) for u,z in outline],.005,.044,shell("cream"),LINER,"chest",follow=surface)
        mark_face(g,"medic",len(outline),follows_surface=True)
    elif s<2:
        # Straight zipper on the flight suit and the resident's short utility jacket.
        mat=cloth("brown" if kit is KITS["Resident"] else "blueSeam")
        ribbon(g,[(0,0,z) for z in (1.064,1.109,1.148,1.192,1.235,1.281,1.320,1.363,1.418,1.454)],
               .007,mat,"spine",follow=surface)
        if kit is KITS["Resident"] and s<=0:
            for sign in (-1,1):
                ribbon(g,[(sign*.061,front_datum(p,kit,1.13)+.008,1.108),
                          (sign*.088,front_datum(p,kit,1.18)+.004,1.198)],
                       .009,cloth("charcoal"),"spine")
                ribbon(g,[(sign*.044,y+.012,1.361),(sign*.088,y+.013,1.360)],
                       .010,cloth("charcoal"),"chest")
    return g


def coat_tail(p,s,side):
    g=Geometry()
    sign=1 if side=="L" else -1
    bone="thigh."+side
    across=choose(s,14,10,7,5)
    rings=[(1.085,.192,.002,.126),(1.007,.220,.012,.158),(.903,.240,.014,.152),
           (.811,.250,.016,.137),(.753,.252,.017,.116)]
    scale=p.hip/.171
    start_angle=.085
    for row,(z,w,c,d) in enumerate(rings):
        for i in range(across):
            angle=start_angle+(pi-start_angle-.18)*i/(across-1)
            # A center-front opening and a lower rear hem leave the knees mobile.
            zz=z-.047*(i/(across-1))*(row/4)
            g.v.append(Vector((sign*w*scale*sin(angle),c-d*cos(angle),zz)))
            g.weights.append(blend("pelvis",bone,min(1,row*.41)))
    for row in range(len(rings)-1):
        for i in range(across-1):
            a=row*across+i
            face=(a,a+1,a+1+across,a+across)
            g.poly(face if sign==1 else tuple(reversed(face)),cloth("sand" if i<across*.73 else "earth"),True)
    if s<2:
        ribbon(g,[(sign*.124*scale,-.078,.793),(sign*.120*scale,-.091,.872),(sign*.105*scale,-.105,.964)],
               .015,shell("coral"),bone)
    return g


def shoulder(s,side,kit,arm,body):
    g=Geometry()
    sign=1 if side=="L" else -1
    columns=choose(s,15,11,7,5)
    stations=[(1.443,.028,.008),(1.417,.046,.013),(1.387,.053,.016),(1.354,.052,.014),
              (1.334,.037,.007)]
    if s>=1:
        stations=[stations[i] for i in (0,1,3,4)]
    material=shell("blue" if kit is KITS["Guard"] else "rust" if kit is KITS["Pilot"] else "cream")
    sources=(arm,body)
    trees=[BVHTree.FromPolygons(org.v,org.faces) for org in sources]
    def location(z,y,offset):
        hits=[tree.ray_cast(Vector((sign,y,z)),Vector((-sign,0,0))) for tree in trees]
        valid=[(hit,org) for hit,org in zip(hits,sources) if hit[0] is not None]
        if not valid:
            return Vector((sign*.18,y,z)),{"upper_arm."+side:1}
        (co,normal,polygon,_),org=min(valid,key=lambda row:row[0][3])
        ids=org.faces[polygon]
        distances=[1/max((co-org.v[i]).length_squared,1e-9) for i in ids]
        total=sum(distances)
        weights={}
        for idx,d in zip(ids,distances):
            for bone,weight in org.weights[idx].items():
                weights[bone]=weights.get(bone,0)+d*weight/total
        return co+Vector((sign*offset,0,0)),weights
    for z,width,offset in stations:
        for i in range(columns):
            t=2*i/(columns-1)-1
            vertex,weights=location(z+.010*abs(t)**2,width*t-.004,offset)
            g.v.append(vertex)
            g.weights.append(weights)
    for row in range(len(stations)-1):
        for i in range(columns-1):
            a=row*columns+i
            color=material
            if kit is KITS["Explorer"] and row==2 and columns*.19<=i<columns*.55:
                color=shell("orange")
            stripe=i+.57*row
            if kit is KITS["Guard"] and row>=1 and s<2 and (
                    columns*.22<=stripe<columns*.35 or columns*.47<=stripe<columns*.60):
                color=shell("stone")
            if kit is KITS["Guard"] and s==0 and row==1 and i==(columns-1)//2:
                color=shell("cyan")
            face=(a,a+columns,a+columns+1,a+1)
            g.poly(face if sign==1 else tuple(reversed(face)),color,True)
    if kit is KITS["Guard"]:
        fit=trace_front(arm,lambda x,z:-.050)
        shape=[(.171,1.342),(.213,1.318),(.282,1.336),(.300,1.376),(.266,1.440),
               (.208,1.455),(.174,1.423)]
        weight=blend("clavicle."+side,"upper_arm."+side,.68)
        plate(g,[(sign*x,z) for x,z in shape],-.009,.026,shell("blue"),LINER,weight,follow=fit)
        if s<2:
            enamel=trace_front(g,fit)
            ribbon(g,[(sign*.236,0,1.351),(sign*.235,0,1.373),(sign*.226,0,1.389)],
                   .010,shell("cyan"),weight,follow=enamel)
    return g


def leg_plate(rig,p,s,side,kit,leg):
    g=Geometry()
    sign=1 if side=="L" else -1
    top=rig.bones["thigh."+side]
    knee=top.tail_local
    knee_x=sign*leg_lateral(p,abs(knee.x),.050,upper=True)
    plate_y=-.061*p.leg-kit.clearance
    surface=trace_front(leg,lambda x,z:plate_y+.007)
    hard=(KITS["Explorer"],KITS["Guard"],KITS["NPC_HeavyCombat"],KITS["NPC_Miner"])
    if kit in (*hard,KITS["Pilot"],KITS["Resident"]):
        mat=kit.armor if kit in (KITS["Explorer"],KITS["Guard"]) else "rust" if kit is KITS["Pilot"] else "earth"
        if kit in (KITS["NPC_HeavyCombat"],KITS["NPC_Miner"]):
            mat="iron" if kit is KITS["NPC_HeavyCombat"] else "gunmetal"
        bone="thigh."+side
        cx=sign*(leg_lateral(p,.109,.074,upper=True)+.035*p.leg/body_base(p).leg)
        z= .795 if kit is KITS["Explorer"] else .780
        panel(g,cx,.004,z,.108 if kit is KITS["Guard"] else .104,
              .31 if kit in (KITS["Guard"],KITS["NPC_HeavyCombat"]) else .246,.031,
              shell(mat) if kit is not KITS["Resident"] else cloth(mat),
              LINER,bone,follow=surface)
        armor=trace_front(g,surface)
        if s<=0 and kit is KITS["Guard"]:
            for offset in (-.016,.013):
                ribbon(g,[(cx-.037,plate_y-.016,z-.099+offset),(cx+.038,plate_y-.014,z-.052+offset)],
                       .012,shell("stone"),bone,follow=armor)
        if s<=0 and kit is KITS["Explorer"]:
            panel(g,cx+sign*.026,.005,z,.029,.132,.012,shell("gunmetal"),LINER,bone,follow=armor)
        if s<=0 and kit is KITS["Resident"]:
            panel(g,cx,.004,z+.066,.095,.039,.011,cloth("khaki"),LINER,bone,follow=armor)
    if s<2:
        mat="cream" if kit is KITS["Medic"] else "stone" if kit is KITS["Explorer"] else "gunmetal"
        ky=knee.y-.061*p.leg-kit.clearance
        panel(g,knee_x,ky,.548,.109,.151,.020,shell(mat),LINER,blend(top.name,"shin."+side,.54))
        if s<=0 and kit in (KITS["Explorer"],KITS["Pilot"]):
            for z in (.516,.542,.570):
                ribbon(g,[(knee_x-.039,ky-.016,z),(knee_x+.039,ky-.016,z)],
                       .012,cloth("charcoal"),blend(top.name,"shin."+side,.54))
        if kit in hard:
            z=.339
            shin_x=sign*leg_lateral(p,.115,.047)
            panel(g,shin_x+sign*.012,-.053*p.leg-kit.clearance,z,.090,.172,.026,
                  shell("cream" if kit is KITS["Explorer"] else "gunmetal"),LINER,"shin."+side)
            if kit is KITS["Explorer"]:
                mark_face(g,"shin",len(OCT))
    return g


def wrist(rig,p,s,side,kit):
    g=Geometry()
    if s>=2:
        return g
    bone=rig.bones["forearm."+side]
    axis=bone.tail_local-bone.head_local
    center=bone.head_local.lerp(bone.tail_local,.73)
    center.x-=(1 if side=="L" else -1)*arm_inset(p)*.56*(1-.73)
    delta=axis.normalized()
    extra=kit.clearance*.65
    r=.036*p.arm+extra
    n=choose(s,16,12,8,6)
    dark=kit in (KITS["Guard"],KITS["NPC_HeavyCombat"],KITS["NPC_Miner"],KITS["Ria_Engineer"])
    casing=shell("gunmetal" if dark else "cream")
    g.loft([center+delta*t for t in (-.031,-.023,.024,.038)],
           [(r*.97,r*.94),(r*1.13,r*1.10),(r*1.12,r*1.10),(r*.92,r*.91)],
           n,casing,{bone.name:1},axis=axis,power=.80)
    if kit is KITS["Explorer"] and side=="R":
        y=center.y-r*1.04
        panel(g,center.x,y,center.z,.091,.084,.026,shell("stone"),LINER,bone.name)
        panel(g,center.x,y-.024,center.z,.064,.046,.009,shell("gunmetal"),LINER,bone.name)
        block(g,(center.x,y-.036,center.z),(.042,.004,.010),"amber",bone.name)
    elif kit in (KITS["Medic"],KITS["Pilot"]) and s<=0:
        block(g,(center.x,center.y-r-.008,center.z),(.035,.013,.028),"gunmetal",bone.name)
        block(g,(center.x,center.y-r-.016,center.z),(.020,.004,.008),"amber",bone.name)
    if kit in (KITS["NPC_HeavyCombat"],KITS["NPC_Miner"]):
        back=bone.head_local.lerp(bone.tail_local,.53)
        back.x-=(1 if side=="L" else -1)*arm_inset(p)*.56*(1-.53)
        panel(g,back.x,back.y-r*.78,back.z,.104,.122,.029,shell("iron"),LINER,bone.name,
              obl=HEX_BEVEL if s<=0 else RECT)
    return g


def belt(p,s,kit):
    g=Geometry()
    n=choose(s,22,16,10,8)
    z=1.038 if kit is KITS["Resident"] else 1.085
    _,cy,d=torso_size(p,s,kit,z)
    y=cy-d
    mat=shell("stone") if kit in (KITS["Explorer"],KITS["Pilot"]) else cloth("charcoal")
    def curve(co,k,a):
        z=heights[k]
        if kit is KITS["Pilot"]:
            z+=.026*abs(cos(a))
        rx,cy,ry=torso_size(p,s,kit,z)
        grow=(.008,.016,.013)[k]
        co.x=copysign(abs(cos(a))**.84*(rx+grow),cos(a))
        co.y=cy+copysign(abs(sin(a))**.84*(ry+grow),sin(a))
        co.z=z
        return co
    heights=(z-.025,z-.017,z+.024)
    g.loft([(0,0,h) for h in heights],[(1,1)]*3,n,mat,
           [spine_weight(h) for h in heights],distortion=curve,caps=False)
    if s<2:
        panel(g,0,y-.005,z,.078,.053,.026,shell("gunmetal"),shell("black"),"pelvis",obl=OCT)
        block(g,(0,y-.032,z),(.039,.007,.011),"amber" if kit is not KITS["Guard"] else "gunmetal","pelvis")
    if kit is KITS["Pilot"]:
        scale=p.gap
        for side in (-1,1):
            xside=side*.114*scale
            bone="thigh."+("L" if side==1 else "R")
            ribbon(g,[(side*.060,y-.020,z-.016),(side*.088*scale,y-.022,z-.061),
                      (side*.140*scale,y+.006,z-.141),(side*.166*scale,-.012,z-.163),(xside,.068,z-.147)],
                   .036,cloth("charcoal"),
                   blend("pelvis",bone,.40),axis=(0,1,.25))
            ribbon(g,[(xside,.068,z-.147),(side*.091*scale,.095,z-.055),(side*.073,.094,z+.004)],
                   .030,cloth("charcoal"),blend("pelvis",bone,.40),axis=(0,1,.25))
    return g


def pack(p,s,kit):
    g=Geometry()
    y=p.back+kit.clearance+.006
    bone="back_socket"
    if kit is KITS["Explorer"]:
        outline=HEX_BEVEL if s>=2 else OCT
        panel(g,0,y,1.311,.300,.333,.061,shell("gunmetal"),LINER,bone,True,obl=outline)
        for sign in (-1,1):
            panel(g,sign*.078,y+.048,1.318,.140,.268,.091,shell("cream"),shell("stone"),bone,True,obl=outline)
            mark_face(g,"shin",len(outline),back=True)
        if s<=0:
            block(g,(0,y+.068,1.469),(.098,.038,.047),"black",bone)
            block(g,(0,y+.088,1.471),(.052,.004,.011),"amber",bone)
    elif kit is KITS["Guard"]:
        panel(g,0,y,1.334,.307,.339,.104,shell("gunmetal"),LINER,bone,True)
        if s<2:
            panel(g,0,y+.090,1.335,.230,.246,.024,shell("darkblue"),shell("gunmetal"),bone,True)
            panel(g,0,y+.113,1.340,.099,.099,.011,shell("gunmetal"),LINER,bone,True,
                  obl=((0,-1),(1,0),(0,1),(-1,0)))
            for side in (-1,1):
                block(g,(side*.109,y+.093,1.462),(.022,.024,.042),"cyan",bone)
    elif kit is KITS["Medic"]:
        panel(g,0,y,1.319,.307,.303,.102,shell("gunmetal"),LINER,bone,True)
        if s<2:
            panel(g,0,y+.077,1.320,.272,.258,.038,shell("cream"),shell("stone"),bone,True)
            mark_face(g,"medic",len(OCT),back=True)
            panel(g,0,y+.106,1.270,.063,.108,.017,shell("gunmetal"),LINER,bone,True)
            block(g,(0,y+.126,1.240),(.038,.008,.012),"amber",bone)
            if s<=0:
                ribbon(g,[(-.054,y+.051,1.457),(-.045,y+.051,1.494),(-.024,y+.055,1.509),
                          (.029,y+.055,1.509),(.048,y+.051,1.494),(.053,y+.051,1.457)],
                       .015,LINER,bone)
    elif kit is KITS["Pilot"]:
        panel(g,0,y,1.354,.257,.256,.089,shell("cream"),LINER,bone,True,
              obl=((-1,-.62),(-.67,-1),(.67,-1),(1,-.62),(.86,.72),(.59,1),(-.59,1),(-.86,.72)))
        if s<2:
            for side in (-1,1):
                outline=OCT if s<=0 else ((-1,-1),(1,-1),(1,1),(-1,1))
                panel(g,side*.094,y+.069,1.355,.048,.157,.022,shell("rust"),LINER,bone,True,obl=outline)
            panel(g,0,y+.084,1.357,.044,.104,.015,shell("gunmetal"),LINER,bone,True,obl=outline)
            block(g,(0,y+.102,1.356),(.011,.004,.060),"amber",bone)
    else:
        panel(g,0,y,1.313,.266,.326,.103,shell("charcoal"),LINER,bone,True)
        if s<2:
            panel(g,0,y+.086,1.319,.228,.254,.028,shell("khaki"),LINER,bone,True)
            panel(g,0,y+.108,1.276,.170,.131,.013,cloth("brown"),LINER,bone,True)
            block(g,(0,y+.124,1.243),(.066,.008,.010),"amber",bone)
            for side in (-1,1):
                ribbon(g,[(side*.084,y+.087,1.197),(side*.091,y+.110,1.317),
                          (side*.087,y+.100,1.426)],.027,LINER,bone)
    return g


def console(p,s,kit):
    g=Geometry()
    if s>=2:
        return g
    y=front_datum(p,kit,1.33)-.028
    z=1.307
    bone="chest"
    if kit is KITS["Explorer"]:
        panel(g,0,y,z,.149,.104,.046,shell("stone"),LINER,bone)
        panel(g,0,y-.041,z,.108,.074,.013,shell("gunmetal"),LINER,bone)
        block(g,(0,y-.059,z),(.073,.006,.011),"amber",bone)
        if s<=0:
            fittings(g,s,[-.060,.060],y-.048,z+.027,bone,"darkblue")
    if kit is KITS["Medic"]:
        panel(g,0,y,z,.206,.109,.031,shell("gunmetal"),LINER,bone)
        for i in (-1,0,1):
            x=i*.066
            obl=OCT if s<=0 else ((-1,-1),(1,-1),(1,1),(-1,1))
            panel(g,x,y-.023,.001+z,.057,.091,.027,shell("glassBack"),shell("stone"),bone,obl=obl)
            if s<1:
                panel(g,x,y-.048,z+.008,.040,.055,.006,glass("cartridge"),shell("stone"),bone,
                      obl=((-1,-1),(1,-1),(1,1),(-1,1)))
            block(g,(x,y-.051,z-.032),(.033,.007,.009),"amber",bone)
    return g


def hip_pouch(p,s,kit):
    g=Geometry()
    if s>=2:
        return g
    sign=-1 if kit is KITS["Medic"] else 1
    x=sign*(p.hip*.79)
    y=-p.belly*.80-kit.clearance
    z=.988 if kit is KITS["Medic"] else .992
    bone="pelvis"
    mat=shell("cream" if kit is KITS["Medic"] else "khaki")
    panel(g,x,y,z,.119,.139 if kit is KITS["Medic"] else .100,.047,mat,LINER,bone)
    if s<=0:
        panel(g,x,y-.037,z+.018,.104,.068,.016,mat,shell("charcoal"),bone)
        block(g,(x,y-.057,z-.026),(.047,.011,.019),"gunmetal",bone)
        block(g,(x,y-.064,z-.026),(.035,.004,.006),"amber",bone)
    return g


def helmet(s):
    g=Geometry()
    n=choose(s,30,15,10,10)
    rows=[(1.526,.067,.072,-.010),(1.548,.088,.097,-.008),(1.584,.109,.111,-.007),
          (1.632,.117,.129,.000),(1.700,.119,.126,.003),(1.764,.108,.116,.008),
          (1.806,.081,.087,.013),(1.833,.043,.049,.017),(1.842,.013,.017,.018)]
    indices=choose(s,range(9),range(9),(0,1,2,4,5,8),(0,2,4,5,7,8))
    rows=[rows[i] for i in indices]
    cream=shell("cream")
    g.loft([(0,y,z) for z,a,b,y in rows],[(a,b) for z,a,b,y in rows],n,cream,
           {"head":1},power=.90)
    for j in range(len(rows)-1):
        z=(rows[j][0]+rows[j+1][0])*.5
        for k in range(n):
            t=tau*(k+.5)/n
            if 1.583<z<1.758 and sin(t)<-.32:
                material=glass("smoke") if s<2 else shell("glassDark")
            elif (z>=1.758 or sin(t)>.73 and z>1.580) and abs(cos(t))<.27:
                material=shell("rust")
            elif 1.566<=z<1.588 and sin(t)<-.25:
                material=shell("stone")
            else:
                material=cream
            g.materials[1+j*n+k]=material
    if s<2:
        nc=choose(s,14,7,6,6)
        for side in (-1,1):
            a=(.107,.115,.132,.136) if s<=0 else (.107,.127,.136)
            r=[(.038,.039),(.044,.044),(.044,.044),(.035,.035)] if s<=0 else [
                (.037,.039),(.044,.044),(.035,.035)]
            g.loft([(side*x,.006,1.663) for x in a],r,nc,
                   shell("gunmetal"),{"head":1},axis=(side,0,.1))
            if s<=0:
                block(g,(side*.137,.002,1.658),(.008,.034,.009),"amber","head")
    return g


def kit_parts(name,p,s,rig,body,arms,legs):
    kit=KITS[name]
    yield "Collar",collar(s,p,kit)
    yield "ChestShell" if name in ("Guard","Explorer","Medic") else "JacketTrim",chest_shell(p,s,kit,body)
    yield "ShoulderStraps",harness(p,s,kit)
    yield "FlightHarness" if name=="Pilot" else "Belt",belt(p,s,kit)
    suffix={"Explorer":"DualLifeSupport","Guard":"DorsalPack","Medic":"RescuePack",
            "Pilot":"FlightSupportPack","Resident":"CommuterPack"}[name]
    yield suffix,pack(p,s,kit)
    if name in ("Explorer","Medic"):
        yield "ChestConsole" if name=="Explorer" else "TriageRack",console(p,s,kit)
    if name in ("Medic","Resident"):
        yield "MedicalPouch" if name=="Medic" else "HipPouch",hip_pouch(p,s,kit)
    if name=="Pilot":
        yield "Helmet",helmet(s)
    for side in ("L","R"):
        if name=="Medic":
            yield "CoatTail_"+side,coat_tail(p,s,side)
        if name!="Resident":
            yield "ShoulderCap_"+side,shoulder(s,side,kit,arms[side],body)
            yield "WristScanner_R" if side=="R" and name=="Explorer" else "Cuff_"+side,wrist(rig,p,s,side,kit)
        if name=="Guard" or name=="Explorer" or s<2:
            yield "LegPanels_"+side,leg_plate(rig,p,s,side,kit,legs[side])
