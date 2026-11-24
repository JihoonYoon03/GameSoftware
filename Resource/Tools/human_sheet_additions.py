"""Additional painted character sheets and their wearable designs."""
from math import cos, pi, sin, tau

from mathutils import Vector

from human_sheet_equipment import HEX_BEVEL, RECT, belt, block, front_datum, harness, leg_plate, mark_face, shoulder, trace_front, wrist
from human_sheet_forms import (
    Geometry, KITS, LINER, OCT, arm_inset, blend, choose, cloth, collar, glass, leg_lateral, panel, plate,
    ribbon, shell, spine_weight,
)

GUARD_VARIANT = "NPC_Guard_Helmet"
NEW_CHARACTERS = ("Player_Explorer","Ria_Engineer","NPC_Researcher","NPC_Miner","NPC_HeavyCombat",GUARD_VARIANT)


def front_disk(g,x,y,z,r,depth,mat,bone,n):
    g.loft([(x,y,z),(x,y-depth*.22,z),(x,y-depth,z)],[(r*.82,r*.82),(r,r),(r*.87,r*.87)],
           n,mat,{bone:1},axis=(0,-1,0))


def helmet(s,heavy=False):
    g=Geometry()
    main="gunmetal" if heavy else "blue"
    accent="amber" if heavy else "cyan"
    n=choose(s,24,16,11,8)
    shape=[(1.561,.074,.077,.010),(1.585,.100,.109,-.008),(1.618,.116,.126,-.014),
           (1.657,.122,.129,-.012),(1.698,.122,.132,-.009),(1.748,.111,.120,.004),
           (1.792,.085,.088,.012),(1.826,.046,.051,.014),(1.846,.019,.024,.015)]
    levels=[shape[i] for i in choose(s,range(9),range(9),(0,1,3,4,5,7,8),(0,2,3,4,6,8))]
    widen=1.12 if heavy else 1
    g.loft([(0,y,z+.013*heavy) for z,a,b,y in levels],[(a*widen,b*widen) for z,a,b,y in levels],
           n,shell(main),{"head":1},power=.78)
    for row in range(len(levels)-1):
        z=(levels[row][0]+levels[row+1][0])*.5
        for i in range(n):
            a=tau*(i+.5)/n
            if (1.642 if heavy else 1.628)<z<1.711 and sin(a)<-.28:
                tint="visor"
            elif z<1.623 and sin(a)<-.22:
                tint="black" if z<1.587 else "gunmetal"
            elif abs(cos(a))<.40 and z>1.72:
                tint="iron"
            else:
                tint=main
            g.materials[1+row*n+i]=shell(tint)
    if s<2:
        n_ear=choose(s,12,7,6,6)
        for sign in (-1,1):
            center=sign*(.118 if heavy else .108)
            g.loft([(center,.014,1.665),(center+sign*.016,.014,1.665)],
                   [(.041,.041),(.036,.036)],n_ear,shell("gunmetal"),{"head":1},axis=(sign,0,.08))
        z=1.611 if heavy else 1.719
        if s<=0:
            markers=(-.064,.064) if heavy else (0,)
            for x in markers:
                block(g,(x,-.150 if heavy else -.135,z),(.036 if heavy else .075,.010,.009),accent,"head")
            # Faceted central chin vent, inset below the visor.
            panel(g,0,-.126,1.607,.123 if heavy else .088,.068,.025,shell("iron"),LINER,"head",obl=HEX_BEVEL)
    return g


def hardhat(s):
    g=Geometry()
    n=choose(s,24,16,10,8)
    profile=[(1.719,.109,.125,.005),(1.732,.111,.135,-.006),(1.749,.102,.108,.007),
             (1.788,.092,.096,.015),(1.814,.068,.079,.017),(1.834,.036,.042,.024),(1.841,.013,.020,.025)]
    rows=[profile[i] for i in choose(s,range(7),range(7),(0,1,2,4,6),(0,1,2,4,6))]
    g.loft([(0,y,z) for z,w,d,y in rows],[(w,d) for z,w,d,y in rows],n,shell("ochre"),{"head":1},power=.81)
    for r in range(min(2,len(rows)-1)):
        for i in range(n):
            g.materials[1+r*n+i]=shell("black" if r==0 else "stone")
    if s<2:
        front_disk(g,0,-.112,1.777,.040,.045,shell("gunmetal"),"head",choose(s,14,10,8,8))
        if s<=0:
            front_disk(g,0,-.156,1.777,.027,.005,shell("lamp"),"head",10)
    return g


def respirator(s):
    g=Geometry()
    outline=[(-.044,1.583),(.042,1.583),(.070,1.606),(.056,1.648),
             (.024,1.669),(0,1.675),(-.024,1.669),(-.056,1.648),(-.070,1.606)]
    plate(g,outline,-.076,.049,shell("iron"),LINER,"head",curve=.8)
    if s<2:
        n=choose(s,14,9,7,6)
        for sign in (-1,1):
            front_disk(g,sign*.061,-.081,1.616,.033,.039,shell("stone"),"head",n)
            if s<=0:
                front_disk(g,sign*.061,-.119,1.616,.023,.006,shell("black"),"head",7)
                ribbon(g,[(sign*.048,-.017,1.624),(sign*.083,-.039,1.645),(sign*.061,-.095,1.638)],
                       .016,LINER,"head")
        block(g,(0,-.130,1.610),(.027,.009,.039),"black","head")
    return g


def eye_protection(s,research=False):
    g=Geometry()
    if s>=2:
        return g
    front=glass("researchGlass" if research else "safetyGlass")
    columns=choose(s,15,11,7,6)
    center_z=1.670
    width=.114 if research else .105
    for row in range(3):
        for i in range(columns):
            x=width*(2*i/(columns-1)-1)
            ratio=abs(x/width)
            nose=.024*max(0,1-abs(x)/.027)
            z=(center_z-(.032 if research else .024)+nose,center_z+.010,
               center_z+(.036 if research else .023))[row]
            y=-.116+.071*ratio**2+(.003 if row==0 else 0)
            g.v.append(Vector((x,y,z)))
            g.weights.append({"head":1})
    for row in range(2):
        for i in range(columns-1):
            a=row*columns+i
            g.poly((a,a+1,a+columns+1,a+columns),front,True)
    for variant,z in (("iron",1.706),("black",1.703)):
        z-=0 if research else .013
        path=[(width*t,-.117+.071*t*t,z) for t in (-1,-.69,-.35,0,.35,.69,1)]
        ribbon(g,path,.008 if variant=="iron" else .003,shell(variant),"head")
    for side in (-1,1):
        strap=[(side*(width-.003),-.048,1.695),(side*.102,-.011,1.692),(side*.093,.045,1.684),
               (side*.082,.090,1.663)]
        ribbon(g,strap,.015,LINER,"head",axis=(1,0,0))
        if s<=0:
            block(g,(side*(width-.003),-.047,1.689),(.016,.019,.020),"iron","head")
    return g


def power_pack(p,s,kind):
    g=Geometry()
    bone="back_socket"
    kit=KITS[kind]
    y=p.back+kit.clearance+.001
    w,d,h={"Ria_Engineer":(.357,.157,.340),"NPC_Researcher":(.257,.132,.327),
           "NPC_Miner":(.319,.190,.354),"NPC_HeavyCombat":(.302,.180,.325)}[kind]
    z=1.326
    panel(g,0,y,z,w,h,d*.72,shell("gunmetal"),LINER,bone,True,
          obl=HEX_BEVEL if s>=2 else OCT)
    if kind=="NPC_Researcher":
        if s>=1:
            panel(g,0,y+d*.69,z,w*.79,h*.70,d*.24,shell("purpleMetal"),LINER,bone,True,obl=RECT)
        else:
            for i in (-1,0,1):
                x=i*w*.264
                g.loft([(x,y+d*.72,z+t*h) for t in (-.375,-.31,.31,.375)],
                       [(w*.095,d*.135),(w*.102,d*.159),(w*.102,d*.159),(w*.095,d*.135)],
                       6,shell("lilac"),{bone:1})
                for end in (-1,1):
                    block(g,(x,y+d*.75,z+end*.35*h),(w*.192,d*.24,h*.12),"iron",bone)
    elif s<2:
        color="copper" if kind=="NPC_HeavyCombat" else "iron"
        panel(g,0,y+d*.69,z,w*.77,h*.75,d*.28,shell(color),LINER,bone,True)
        if s<=0:
            for side in (-1,1):
                panel(g,side*w*.37,y+d*.82,z,w*.09,h*.62,d*.13,shell("gunmetal"),LINER,bone,True,obl=RECT)
            marker_z=z+.102 if kind in ("Ria_Engineer","NPC_HeavyCombat") else z+.139
            block(g,(0,y+d*.99,marker_z),(.067,.014,.024),"black",bone)
            for i in (-1,0,1):
                block(g,(i*.015,y+d*1.05,marker_z),(.010,.008,.013),"amber",bone)
        else:
            block(g,(0,y+d*.95,z+.102),(.049,.010,.019),"amber",bone)
    return g


def twin_pack(p,s):
    g=Geometry()
    bone="back_socket"
    y=p.back+.022
    outline=HEX_BEVEL if s>=2 else OCT
    panel(g,0,y,1.320,.340,.370,.064,shell("iron"),LINER,bone,True,obl=outline)
    for sign in (-1,1):
        x=sign*.087
        panel(g,x,y+.049,1.320,.147,.287,.115,shell("cream"),shell("stone"),bone,True,obl=outline)
        if s<2:
            ribbon(g,[(x-.019,y+.165,1.243),(x+.019,y+.165,1.373)],.012,shell("orange"),bone)
    if s<=0:
        for z in (1.195,1.451):
            block(g,(0,y+.102,z),(.056,.103,.031),"gunmetal",bone)
    return g


def extra_cuff(p,s,side,rig,kind):
    g=Geometry()
    if s>=2:
        return g
    sign=1 if side=="L" else -1
    bone=rig.bones["forearm."+side]
    axis=bone.tail_local-bone.head_local
    t=.66 if kind=="medic" else .72
    center=bone.head_local.lerp(bone.tail_local,t)
    center.x-=sign*arm_inset(p)*.56*(1-t)
    r=.043*p.arm+.014
    tangent=axis.normalized()
    n=choose(s,16,12,8,6)
    g.loft([center+tangent*k for k in (-.043,-.036,.036,.047)],
           [(r*.93,r*.88),(r,r*.98),(r,r*.98),(r*.94,r*.93)],
           n,LINER,{bone.name:1},axis=axis,power=.91)
    tint={"medic":"cream","research":"iron","explorer":"stone"}[kind]
    accent={"medic":"cyan","research":"lilac","explorer":"amber"}[kind]
    y=center.y-r*.91
    w=.122 if kind=="medic" else .105
    panel(g,center.x,y,center.z,w,.092,.028,shell(tint),LINER,bone.name,obl=OCT)
    panel(g,center.x,y-.024,center.z,w*.78,.055,.009,shell("visor"),LINER,bone.name,
          obl=RECT if s==1 else HEX_BEVEL)
    if s<=0:
        for i in range(4):
            x=center.x+(i-1.5)*w*.125
            block(g,(x,y-.035,center.z+.001),(.011,.003,.020-.004*abs(i-1.5)),accent,bone.name)
    else:
        ribbon(g,[(center.x-.030,y-.035,center.z),(center.x+.030,y-.035,center.z)],.009,shell(accent),bone.name)
    return g


def pouch(p,s,side,style):
    g=Geometry()
    if s>=2:
        return g
    sign=1 if side=="L" else -1
    x=sign*p.hip*.85
    y=-p.belly-.010
    z=.993
    bone="pelvis"
    tint={"utility":"charcoal","medic":"cream","tools":"rust"}[style]
    cover={"utility":"gunmetal","medic":"sand","tools":"brown"}[style]
    w,h,d=(.172,.126,.067) if style=="tools" else (.160,.115,.068)
    if style=="tools":
        g.loft([(x+w*t,y-.027,z) for t in (-.5,-.39,.39,.50)],
               [(h*a,d*b) for a,b in ((.39,.43),(.50,.55),(.50,.55),(.41,.45))],
               choose(s,16,12,8,6),cloth(tint),{bone:1},axis=(1,0,.13),power=.70)
        panel(g,x,y-d*.75,z+.028,w*.88,h*.37,.019,cloth(cover),LINER,bone,obl=RECT if s==1 else OCT)
    else:
        panel(g,x,y,z,w,h,d,cloth(tint),LINER,bone)
        panel(g,x,y-d*.86,z+.020,w*.92,h*.52,.013,cloth(cover),LINER,bone,obl=RECT if s==1 else OCT)
    if s<=0:
        for offset in (-.035,.035) if style=="medic" else (0,):
            ribbon(g,[(x+offset,y-d- .010,z-.049),(x+offset,y-d-.019,z+.050)],.015,LINER,bone)
        block(g,(x,y-d-.020,z-.016),(.033,.012,.035),"metal",bone)
        block(g,(x,y-d-.028,z-.016),(.018,.006,.018),"amber" if style=="medic" else "black",bone)
    return g


def work_lamp(p,s,side):
    g=Geometry()
    if s>=2:
        return g
    sign=1 if side=="L" else -1
    x=sign*p.shoulder*.67
    z=1.405
    y=-p.chest*.60-.031
    bone="clavicle."+side
    panel(g,x,y,z,.116,.085,.058,shell("ochre"),LINER,bone)
    panel(g,x,y-.054,z,.078,.054,.014,shell("gunmetal"),LINER,bone,obl=RECT if s==1 else OCT)
    for i in range(3 if s<=0 else 1):
        dx=(i-1)*.020 if s<=0 else 0
        block(g,(x+dx,y-.074,z),(.013 if s<=0 else .047,.005,.025),"lamp",bone)
    if s<=0:
        block(g,(x,y-.018,z+.050),(.063,.036,.015),"iron",bone)
    return g


def front_gear(p,s,kit,body):
    g=Geometry()
    fit=trace_front(body,lambda x,z:front_datum(p,kit,z)+.020)
    if kit is KITS["Player_Explorer"]:
        w=p.rib
        shape=[(-.79,1.269),(-1.02,1.344),(-.79,1.422),(-.47,1.430),(-.33,1.400),
               (.33,1.400),(.47,1.430),(.79,1.422),(1.02,1.344),(.79,1.269),(.36,1.242),(-.36,1.242)]
        plate(g,[(x*w,z) for x,z in shape],0,.038,shell("cream"),LINER,"chest",follow=fit)
    elif kit in (KITS["Ria_Engineer"],KITS["NPC_Researcher"]):
        engineer=kit is KITS["Ria_Engineer"]
        main=cloth("ochre" if engineer else "teal")
        shape=([(0,1.081),(.084,1.082),(.137,1.210),(.105,1.415),(.038,1.384),(-.071,1.209)] if engineer else
               [(-.104,1.090),(.116,1.090),(.134,1.296),(.054,1.433),(-.126,1.373),(-.043,1.257)])
        plate(g,shape,.002,.026,main,LINER,blend("spine","chest",.52),follow=fit)
        if not engineer:
            mark_face(g,"research",len(shape),follows_surface=True)
        elif s<2:
            outer=trace_front(g,fit)
            ribbon(g,[(.089,0,1.390),(.025,0,1.267),(-.052,0,1.125)],.010,
                   cloth("rust"),"spine",follow=outer)
    elif kit is KITS["NPC_Miner"]:
        plate(g,[(-.154,1.184),(.154,1.184),(.174,1.359),(.124,1.433),(.052,1.431),
                 (-.052,1.431),(-.124,1.433),(-.174,1.359)],.001,.035,
              shell("gunmetal"),LINER,"chest",follow=fit)
        mark_face(g,"miner",8,follows_surface=True)
    else:
        shape=[(-.157,1.276),(.157,1.276),(.198,1.366),(.164,1.419),(.097,1.449),
               (.067,1.416),(-.067,1.416),(-.097,1.449),(-.164,1.419),(-.198,1.366)]
        plate(g,shape,.004,.059,shell("iron"),LINER,"chest",follow=fit)
        mark_face(g,"heavy",len(shape),follows_surface=True)
        for i in range(3 if s<2 else 1):
            z=1.234-i*.071
            panel(g,0,.001,z,.262-.027*i,.067,.035,shell("gunmetal"),LINER,spine_weight(z),
                  obl=RECT if s>=2 else HEX_BEVEL,follow=fit)
    return g


def navigation_console(p,s,kit,body):
    g=Geometry()
    if s>=2:
        return g
    fit=trace_front(body,lambda x,z:front_datum(p,kit,z))
    y=fit(0,1.328)-.029
    panel(g,0,y,1.328,.219,.133,.050,shell("stone"),LINER,"chest")
    panel(g,0,y-.048,1.328,.158,.077,.008,shell("visor"),LINER,"chest",obl=RECT)
    if s<=0:
        panel(g,-.027,y-.058,1.328,.053,.042,.005,shell("amber"),LINER,"chest",obl=RECT)
        front_disk(g,.035,y-.058,1.329,.022,.004,shell("orange"),"chest",8)
    else:
        block(g,(0,y-.057,1.33),(.102,.004,.026),"amber","chest")
    return g


def field_coat(p,s,side,engineering=False):
    """Hem flaps are attached at the belt and follow the thigh toward their open ends."""
    g=Geometry()
    sign=1 if side=="L" else -1
    bone="thigh."+side
    n=choose(s,17,13,9,6)
    if engineering and side=="R":
        return g
    height=(1.097,1.009,.860,.766) if engineering else (1.105,1.018,.864,.710,.568)
    scales=(1.,1.08,1.14,1.19,1.25)
    width=p.hip+.042
    back=p.seat*.94+.034
    front=p.belly+.040
    for j,z in enumerate(height):
        scale=scales[j]
        for i in range(n):
            t=i/(n-1)
            angle=.146+(2.12 if engineering else 2.81)*t
            zz=z
            if j==len(height)-1:
                zz+=.066*(.52-t)*sign if engineering else .074*t+.015*(sign<0)
            x=sign*width*scale*sin(angle)
            y=-front*scale*cos(angle) if cos(angle)>0 else -back*scale*cos(angle)
            g.v.append(Vector((x,y,zz)))
            initial=blend("spine","pelvis",.71)
            weight=(initial if j==0 else blend("pelvis",bone,.78) if j==1
                    else {bone:1} if j<4 else blend(bone,"shin."+side,.30))
            g.weights.append(weight)
    for j in range(len(height)-1):
        for i in range(n-1):
            a=j*n+i
            face=(a,a+1,a+1+n,a+n)
            tint="ochre" if engineering else "teal" if sign>0 and i<n*.64 else "dust"
            g.poly(face if sign==1 else tuple(reversed(face)),
                   cloth("khaki" if i==0 or i==n-2 else tint),True)
    return g


def reinforced_shoulder(s,side,kit,arm,body):
    g=shoulder(s,side,kit,arm,body)
    original=list(g.v)
    sign=1 if side=="L" else -1
    heavy=kit is KITS["NPC_HeavyCombat"]
    rows=5 if s<=0 else 4
    cols=len(g.v)//rows
    for i,co in enumerate(g.v):
        co.x+=sign*(.025 if heavy else .007)
    for i,mat in enumerate(g.materials):
        row,col=divmod(i,cols-1)
        tint=("copper" if row==0 or col<2 else "gunmetal") if heavy else "ochre"
        if row==rows-2:
            tint="iron" if heavy else "black"
        g.materials[i]=shell(tint)
    if s<2 and heavy:
        start=len(g.v)
        for i,co in enumerate(original):
            if i//cols<rows-2:
                continue
            g.v.append(co+Vector((sign*.011,0,-.015)))
            g.weights.append(g.weights[i])
        for i in range(cols-1):
            a=start+i
            face=(a,a+cols,a+cols+1,a+1)
            g.poly(face if sign==1 else tuple(reversed(face)),shell("gunmetal"),True)
    center=.260 if heavy else .240
    width=.212 if heavy else .158
    height=.172 if heavy else .129
    z=1.413 if heavy else 1.412
    bone=blend("clavicle."+side,"upper_arm."+side,.73)
    shape=[(sign*(center+u*width*.5),z+v*height*.5) for u,v in OCT]
    surface=trace_front(arm,lambda x,z:-.061 if heavy else -.050)
    plate(g,shape,-.011,.032 if heavy else .021,shell("gunmetal" if heavy else "ochre"),
          LINER,bone,follow=surface)
    if heavy and s<=0:
        top=trace_front(g,surface)
        plate(g,[(sign*(center+u*.072),1.443+v*.032) for u,v in HEX_BEVEL],.003,.012,
              shell("copper"),LINER,bone,follow=top)
    return g


def knee_drive(p,s,side):
    g=Geometry()
    sign=1 if side=="L" else -1
    thigh="thigh."+side
    shin="shin."+side
    x=sign*(leg_lateral(p,.115,.05,upper=True)+.097)
    if s<2:
        panel(g,x,-.014,.695,.057,.234,.041,shell("gunmetal"),LINER,thigh,obl=HEX_BEVEL)
        front_disk(g,x,-.052,.551,.032,.026,shell("iron"),shin,choose(s,12,8,6,6))
        panel(g,x,-.025,.382,.045,.251,.046,shell("metal"),LINER,shin,obl=RECT)
        if s<=0:
            for z in (.295,.421):
                block(g,(x,-.073,z),(.058,.015,.048),"gunmetal",shin)
    else:
        panel(g,x,-.019,.423,.044,.270,.039,shell("gunmetal"),LINER,shin,obl=RECT)
    return g


def extra_parts(tag,p,s,rig,body,arms,legs):
    kit=KITS[tag]
    explorer=tag=="Player_Explorer"
    research=tag=="NPC_Researcher"
    engineer=tag=="Ria_Engineer"
    miner=tag=="NPC_Miner"
    heavy=tag=="NPC_HeavyCombat"
    yield "Collar",collar(s,p,kit)
    yield ("ChestShell" if explorer or heavy or miner else "JacketTrim"),front_gear(p,s,kit,body)
    yield "ShoulderStraps",harness(p,s,kit)
    yield "Belt",belt(p,s,kit)
    if explorer:
        yield "DualLifeSupport",twin_pack(p,s)
        yield "ChestConsole",navigation_console(p,s,kit,body)
        yield "WristScanner_L",extra_cuff(p,s,"L",rig,"explorer")
        yield "UtilityPouch_R",pouch(p,s,"R","utility")
    else:
        name="SampleRack" if research else "DorsalPowerPack" if heavy else "IndustrialPowerRig"
        yield name,power_pack(p,s,tag)
    if engineer or research:
        yield "ResearchVisor" if research else "SafetyGoggles",eye_protection(s,research)
    if research:
        yield "WristScanner_R",extra_cuff(p,s,"R",rig,"research")
        yield "UtilityPouch_R",pouch(p,s,"R","utility")
    if engineer or miner:
        yield "ToolRoll_L",pouch(p,s,"L","tools")
        yield "WorkLamp_R" if miner else "WorkLamp_L",work_lamp(p,s,"R" if miner else "L")
    if miner:
        yield "Hardhat",hardhat(s)
        yield "Respirator",respirator(s)
    if heavy:
        yield "Helmet",helmet(s,heavy=True)
        yield "UtilityPouch_R",pouch(p,s,"R","utility")
    for side in ("L","R"):
        if heavy or miner:
            yield "ShoulderCap_"+side,reinforced_shoulder(s,side,kit,arms[side],body)
        elif explorer:
            yield "ShoulderCap_"+side,shoulder(s,side,KITS["Explorer"],arms[side],body)
        if research or engineer:
            yield "CoatTail_"+side,field_coat(p,s,side,engineering=engineer)
        if (explorer and side=="R") or heavy or miner or engineer:
            yield "Cuff_"+side,wrist(rig,p,s,side,kit)
        leg_style=KITS["Explorer"] if explorer else kit
        yield "LegPanels_"+side,leg_plate(rig,p,s,side,leg_style,legs[side])
        if heavy:
            yield "ExoskeletonStrut_"+side,knee_drive(p,s,side)
