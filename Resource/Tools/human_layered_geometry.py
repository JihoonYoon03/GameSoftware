"""Continuous skin and garment geometry for the HUMAN_7H three-layer contract."""
from collections import defaultdict
from math import cos, exp, pi, sin, tau

from mathutils import Vector

from human_sheet_equipment import mark_faces
from human_sheet_forms import (
    FIGURES, HEAD_SECTIONS, KITS, LINER, SKIN, Geometry, Surface, blend, body_base, choose, cloth,
    features, fitted_body, head, leg_lateral, shell, stitch, tint_faces,
)


def last_limb_ring(g,stage):
    return [(g.v[i],g.weights[i]) for i in g.distal_ring]


def palm(side,stage,ring,pattern,glove=False):
    g=Geometry()
    sign=1 if side=="L" else -1
    bone="hand."+side
    skin=cloth("carbon") if glove else SKIN
    count=choose(stage,18,14,10,6)
    centers=[(sign*.435,-.014,.924),(sign*.445,-.017,.903),(sign*.453,-.022,.882),
             (sign*.461,-.026,.865)]
    shapes=[(.026,.019),(.030,.021),(.034,.021),(.029,.016)]
    g.loft(centers,shapes,count,skin,{bone:1},axis=(sign*.18,-.04,-1),power=.73,caps=(False,True))
    stitch(g,ring,count,skin,(sign*.18,-.04,-1),pattern)
    if stage<1:
        for i in range(4):
            # Tapered individual fingers have unequal lengths and a slight inward curl.
            x=sign*(.437+i*.0134)
            tip=.813+(.006,.001,.004,.011)[i]
            p=[(x,-.024,.873),(x+sign*.006,-.029,.856),(x+sign*.009,-.037,tip+.014),
               (x+sign*.005,-.044,tip+.005),(x+sign*.002,-.044,tip)]
            sizes=[(.0085,.008),(.008,.007),(.0067,.006),(.006,.0054),(.003,.0032)]
            g.loft(p,sizes,choose(stage,9,7,6,5),skin,{bone:1},axis=(sign*.12,-.24,-1),power=.86)
        g.loft([(sign*.427,-.025,.904),(sign*.415,-.036,.882),(sign*.415,-.048,.864),
                (sign*.425,-.049,.845)],
               [(.011,.011),(.011,.010),(.0085,.009),(.0048,.0055)],
               choose(stage,10,8,6,5),skin,{bone:1},axis=(-sign*.16,-.30,-1))
    return g


def sole(p,side,stage,ring,pattern,kit=None):
    g=Geometry()
    sign=1 if side=="L" else -1
    foot="foot."+side
    shin="shin."+side
    x=sign*leg_lateral(p,.115,.030)
    width=.73+.27*p.leg/body_base(p).leg
    n=choose(stage,20,16,12,6)
    guard=kit is KITS["Guard"]
    if kit:
        dark=kit.sheet in ("Protective_Service_Guard.png","NPC_Miner.png","NPC_HeavyCombat.png")
        finish=shell("iron" if dark else "stone")
        fabric=cloth("carbon" if dark else "brown")
        rows=[(.128,-.011,.043,.048),(.116,-.024,.052,.070),(.094,-.042,.058,.088),
              (.064,-.063,.062,.119),(.041,-.069,.066,.126),(.022,-.070,.064,.127),(.002,-.069,.061,.123)]
        if guard:
            rows=[(.128,-.009,.048,.052),(.113,-.022,.050,.062),(.088,-.044,.054,.095),
                  (.058,-.062,.059,.120),(.037,-.065,.063,.126),(.023,-.064,.064,.126),(.002,-.064,.060,.119)]
        if stage==2:
            rows=[rows[i] for i in (0,2,4,6)]
        weights=[blend(foot,shin,max(0,(z-.076)*7.4)) for z,*_ in rows]
        g.loft([(x,y,z) for z,y,*_ in rows],[(w*width,d) for _,_,w,d in rows],n,
               fabric,weights,axis=(0,0,-1),power=.66 if guard else .73,caps=(False,True))
        tint_faces(g,0,len(rows),n,lambda row,theta: LINER if rows[row][0]<.045 else
                   finish if rows[row][0]<.096 or cos(theta)>.73 else fabric,bottom=False)
    else:
        rows=[(.126,.000,.033,.037),(.101,-.014,.038,.060),(.071,-.046,.050,.091),
              (.039,-.060,.053,.103),(.015,-.061,.052,.103),(.003,-.060,.049,.099)]
        if stage==2:
            rows=[rows[i] for i in (0,2,3,5)]
        def toes(co,row,theta):
            # The great toe leads the gentle arc of the forefoot.
            front=max(0,sin(theta))**3
            co.y-=front*(.010*exp(-((sign*(co.x-x)+.026)/.022)**2))
            return co
        g.loft([(x,y,z) for z,y,*_ in rows],[(w*width,d) for _,_,w,d in rows],n,
               SKIN,{foot:1},axis=(0,0,-1),power=.83,caps=(False,True),distortion=toes)
        fabric=SKIN
    stitch(g,ring,n,fabric,(0,0,-1),pattern)
    if kit and stage<2 and not guard:
        # The padded boot collar overlaps the trouser seam and the instep shell keeps its own volume.
        n_cuff=choose(stage,17,14,10,6)
        collar=[(.117,-.018,.049,.061),(.149,-.008,.050,.056),(.188,-.007,.054,.055),
                (.214,-.006,.051,.050)]
        g.loft([(x,y,z) for z,y,*_ in collar],[(a*width,b) for _,_,a,b in collar],n_cuff,finish,
               [blend(foot,shin,max(0,(z-.092)*7.5)) for z,*_ in collar],power=.76,caps=False)
        if stage<=0:
            for y,z,half in ((-.139,.068,.042),(-.113,.098,.034),(-.077,.143,.033)):
                p=[(x-half*width,y+.015,z+.001),(x,y,z+.006),(x+half*width,y+.015,z+.001)]
                from human_sheet_forms import ribbon
                ribbon(g,p,.012,LINER,foot)
            g.panel((x,-.051,.181),(.054*width,.014,.022),shell("gunmetal"),shin,6)
    return g


def merge(pieces,weld=False):
    """Weld identical anatomical boundary vertices without collapsing nearby folds or trim."""
    joined=Geometry()
    joined.visibility=[]
    lookup={}
    for g,zone,finish in pieces:
        remap=[]
        for i,co in enumerate(g.v):
            key=(tuple(round(v,6) for v in co),tuple(sorted(g.weights[i].items())))
            slot=lookup.get(key) if weld and i not in g.vertex_uv else None
            if slot is None:
                slot=len(joined.v)
                joined.v.append(co.copy())
                joined.weights.append(g.weights[i])
                joined.visibility.append(zone if isinstance(zone,int) else zone(i,co))
                if weld and i not in g.vertex_uv:
                    lookup[key]=slot
                if i in g.vertex_uv:
                    joined.vertex_uv[slot]=g.vertex_uv[i]
            remap.append(slot)
        for index,(face,material,smooth) in enumerate(zip(g.faces,g.materials,g.smooth)):
            joined.poly((remap[i] for i in face),finish or material,smooth,g.face_uv.get(index))
    return joined


def body_zone(i,point):
    if point.z<1.089:
        return 2
    return 1


def bare_body(rig,profile,stage,patterns,*,facial_details=True):
    p=FIGURES[profile]
    torso,neck,arms,legs=fitted_body(rig,p,stage,None,"skin",open_ends=True,patterns=patterns)
    skull=head(stage,p,"skin")
    # Keep the head/neck intersection at the same section count as the masked body.
    order=[(torso,body_zone,SKIN),(neck,0,SKIN),(skull,0,SKIN)]
    if facial_details:
        order.append((features(stage,"crop",skull,"skin"),0,None))
    for side in ("L","R"):
        a=arms[side]
        l=legs[side]
        hand=palm(side,stage,last_limb_ring(a,stage),patterns.setdefault("wrist_"+side,{}))
        foot=sole(p,side,stage,last_limb_ring(l,stage),patterns.setdefault("ankle_"+side,{}))
        order.extend(((a,lambda i,co:0 if co.z<.948 else 1,SKIN),(hand,0,SKIN),(l,2,SKIN),(foot,2,SKIN)))
    return merge(order,weld=True)


def fitted_suit(rig,profile,stage,kit,gloves):
    p=FIGURES[profile]
    torso,neck,arms,legs=fitted_body(rig,p,stage,kit,"skin",open_ends=True)
    if kit is KITS["Resident"]:
        chest=[i for i,face in enumerate(torso.faces)
               if all(torso.v[v].y<-.014 and torso.v[v].z>1.050 for v in face)]
        mark_faces(torso,"resident",chest)
    parts=[(torso,1,None)]
    for side in ("L","R"):
        a=arms[side]
        l=legs[side]
        parts.extend(((a,1,None),(l,2,None)))
        if gloves:
            parts.append((palm(side,stage,last_limb_ring(a,stage),None,glove=True),1,None))
        parts.append((sole(p,side,stage,last_limb_ring(l,stage),None,kit),2,None))
    return merge(parts,weld=True),torso,arms,legs


def face_components(geometry):
    """Identify separate hard shells and their single rigid bone without splitting a shell."""
    neighbours=defaultdict(list)
    for i,face in enumerate(geometry.faces):
        for v in face:
            neighbours[v].append(i)
    visited=set()
    parts=defaultdict(lambda:Geometry())
    for start in range(len(geometry.faces)):
        if start in visited:
            continue
        queued=[start]
        visited.add(start)
        component=[]
        while queued:
            current=queued.pop()
            component.append(current)
            for v in geometry.faces[current]:
                for index in neighbours[v]:
                    if index not in visited:
                        visited.add(index)
                        queued.append(index)
        points=sorted({v for i in component for v in geometry.faces[i]})
        bone_total=defaultdict(float)
        for v in points:
            for bone,value in geometry.weights[v].items():
                bone_total[bone]+=value
        bone=max(bone_total,key=bone_total.get)
        target=parts[bone]
        remap={}
        for v in points:
            remap[v]=len(target.v)
            target.v.append(geometry.v[v].copy())
            target.weights.append({bone:1})
            if v in geometry.vertex_uv:
                target.vertex_uv[remap[v]]=geometry.vertex_uv[v]
            if v in geometry.seam_normals:
                target.seam_normals[remap[v]]=geometry.seam_normals[v]
        for i in sorted(component):
            target.poly((remap[v] for v in geometry.faces[i]),geometry.materials[i],geometry.smooth[i],
                        geometry.face_uv.get(i))
    return parts
