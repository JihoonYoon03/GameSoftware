"""Furniture, facility fixtures and biome assets for the colony kit."""
from math import cos, sin, tau
from random import Random

from colony_building_forms import (Form, BODY, ARMOR, DARK, TRIM,
                                   STONE, GLASS, SIGN, LIGHT, AMBER, PLANT, FOLIAGE, BLUE, OXIDE)


GROUPS = {
    "interior": (600,("bed","chair","table","storage_locker","wardrobe_bay","recovery_kit")),
    "facility": (1200,("shop_counter","display_shelf","bio_scanner","medpod","vehicle_lift","tool_rack","holo_briefing_table")),
    "street": (900,("field_barricade","convoy_wreck","high_voltage_pillar","market_booth","checkpoint_gate")),
    "nature": (700,("rock_erma","rock_seira","rock_orren","tree_erma","plant_seira","tree_orren","prism_vein_a","prism_vein_b")),
}


def chamfer(g,x,y,z,a,b,c,mat,k=.06):
    g.block(x,y,z,a,b,c,mat,k if g.tier<2 else 0)


def legs(g,dx,dy,end,mat=TRIM):
    for x in (-dx,dx):
        for y in (-dy,dy):
            g.block(x,y,end/2,.11,.11,end,mat)


def case(g, x,y,z, dx,dy,dz,mat,tier=0):
    chamfer(g,x,y,z,dx,dy,dz,mat,min(dx,dy)*.13)
    if tier<2:
        g.block(x,y,z-dz*.32,dx*1.025,dy*1.025,dz*.105,ARMOR,min(dx,dy)*.07)


def shelves(g,dx,dy,height,count,door,tier):
    g.block(-dx*.46,0,height/2,.085,dy,height,DARK)
    for y in (-dy*.47,dy*.47):
        chamfer(g,0,y,height/2,dx,.10,height,BODY,.03)
    for i in range(count):
        z=.10+(height-.16)*i/(count-1)
        chamfer(g,0,0,z,dx,dy,.12,ARMOR,.055)
        if tier==0 and i<count-1 and not door:
            g.block(dx*.48,0,z+.065,.024,dy*.84,.034,LIGHT)
    if door:
        for y in (-dy*.25,dy*.25):
            chamfer(g,dx*.45,y,height*.52,.073,dy*.44,height*.83,BODY)
            if tier<2:
                g.block(dx*.51,y*.28,height*.58,.056,.052,.27,TRIM)


def fixture(key,tier):
    g=Form()
    g.tier=tier
    g.section="Primary assembly"
    if key=="bed":
        legs(g,.77,.35,.31)
        chamfer(g,0,0,.35,2.13,1.03,.19,ARMOR)
        case(g,0,0,.50,1.93,.92,.18,STONE,tier)
        chamfer(g,-.96,0,.56,.13,1.02,.70,BODY)
        if tier<2:
            chamfer(g,-.66,0,.65,.44,.72,.12,BODY,.075)
            g.block(.02,0,.61,.75,.94,.038,GLASS)
    elif key=="chair":
        legs(g,.21,.22,.44)
        case(g,0,0,.50,.57,.58,.13,STONE,tier)
        for y in (-.24,.24):
            g.beam((-.24,y,.49),(-.35,y,.98),.068,ARMOR)
        chamfer(g,-.32,0,.85,.125,.47,.33,BODY)
        if tier<2:
            for y in (-.305,.305):
                g.beam((-.30,y,.72),(.16,y,.70),.058,TRIM)
    elif key in {"table","holo_briefing_table"}:
        h=.79 if key=="table" else .94
        dx,dy=(1.36,.82) if h<.9 else (1.46,1.10)
        for x in (-.45,.45):
            g.block(x,0,.058,.37,dy*.86,.116,ARMOR,.08)
            g.beam((x*.91,0,.1),(x*.74,0,h-.09),.15,TRIM)
        case(g,0,0,h-.061,dx,dy,.13,STONE if h<.9 else BODY,tier)
        if key=="holo_briefing_table":
            g.profile(0,0,[(h,.29),(h+.105,.27),(h+.15,.20)],GLASS,(18,12,8,8)[tier])
            if tier<2:
                g.profile(0,0,[(h+.151,.16),(h+.165,.164)],LIGHT,14)
    elif key in {"storage_locker","wardrobe_bay","display_shelf","tool_rack"}:
        h=1.95 if key in {"storage_locker","wardrobe_bay"} else 1.63
        d=.55 if key!="wardrobe_bay" else .74
        w={"storage_locker":.79,"wardrobe_bay":1.28,"display_shelf":1.46,"tool_rack":1.19}[key]
        count=2 if key=="wardrobe_bay" else (3 if tier>=2 else 5)
        shelves(g,d,w,h,count,key=="storage_locker",tier)
        if key=="wardrobe_bay":
            g.tube((0,-w*.44,h*.89),(0,w*.44,h*.89),.032,.032,TRIM,8 if tier<2 else 5)
            chamfer(g,d*.32,w*.48,1.21,.12,.085,.24,SIGN)
            g.block(0,0,.33,d*.89,w*.85,.086,BLUE,.05 if tier<2 else 0)
        if key=="tool_rack" and tier==0:
            for i in range(5):
                g.beam((-.11,-.38+i*.179,.75),(.11,-.38+i*.179,.91+i*.041),.064,AMBER if i%2==0 else TRIM)
    elif key=="recovery_kit":
        case(g,0,0,.12,.57,.38,.24,BODY,tier)
        seam=.161
        g.block(0,0,seam,.57,.384,.024,DARK,.039)
        g.block(.29,0,.15,.045,.085,.11,TRIM)
        g.block(-.076,0,.249,.18,.041,.017,LIGHT)
        g.block(-.076,0,.251,.046,.144,.017,LIGHT)
        if tier<2:
            for y in (-.101,.101):
                g.block(.126,y,.26,.040,.05,.04,ARMOR)
            g.block(.126,0,.287,.035,.235,.050,ARMOR,.015)
    elif key=="shop_counter":
        chamfer(g,-.09,0,.44,.60,1.92,.88,BODY)
        case(g,0,0,.91,.82,2.10,.16,STONE,tier)
        g.block(.224,0,.63,.07,.84,.32,ARMOR,.014)
        g.block(.265,0,.65,.023,.47,.082,SIGN)
        if tier<2:
            g.block(-.056,.72,1.158,.19,.34,.32,TRIM,.028)
            g.block(.035,.72,1.19,.028,.224,.16,GLASS)
    elif key in {"bio_scanner","medpod"}:
        dx,dy=(1.57,.84) if key=="bio_scanner" else (2.24,1.08)
        case(g,0,0,.145,dx*.72,dy*.89,.29,ARMOR,tier)
        case(g,0,0,.43,dx,dy,.30,STONE,tier)
        g.block(.14,0,.62,dx*.79,dy*.82,.13,BODY,.10)
        count=(14,10,6,6)[tier]
        start=-.32*dx if key=="medpod" else -.18*dx
        end=.51*dx if key=="medpod" else -.02*dx
        shell=[[(x,dy*.45*cos(i*3.14159265/count),.65+.46*sin(i*3.14159265/count))
                for i in range(count+1)] for x in (start,end)]
        g.rings(shell,GLASS if key=="medpod" else ARMOR)
        if tier<2:
            for y in (-dy*.48,dy*.48):
                g.block(0,y,.489,dx*.58,.052,.043,LIGHT)
            g.block(start-.06,0,.69,.18,dy*.80,.19,BLUE,.04)
    elif key=="vehicle_lift":
        g.block(0,0,.049,1.15,1.68,.098,ARMOR)
        for y in (-.68,.68):
            chamfer(g,0,y,.095,3.2,.39,.19,ARMOR)
            chamfer(g,0,y,.65,2.96,.47,.14,TRIM)
            for sign in (-1,1):
                g.beam((-.93*sign,y,.16),(.93*sign,y,.59),.135,BODY)
            if tier<2:
                g.block(1.28,y,.73,.43,.49,.065,OXIDE)
    elif key in {"field_barricade","checkpoint_gate"}:
        w=1.84 if key=="field_barricade" else 3.26
        h=.83 if key=="field_barricade" else 1.16
        for y in (-w*.39,w*.39):
            chamfer(g,0,y,.10,.65,.40,.20,ARMOR)
            g.beam((-.08,y,.13),(.04,y,h),.12,TRIM)
        chamfer(g,.04,0,h-.16,.26,w,.31,BODY)
        if tier<2:
            for i in range(4):
                y=(i-1.5)*w*.22
                g.beam((.181,y-.09,h-.27),(.181,y+.075,h-.044),.060,AMBER)
        if key=="checkpoint_gate":
            g.block(-.12,w*.448,.76,.36,.28,1.48,ARMOR,.046)
            g.block(.085,w*.448,1.34,.064,.17,.125,SIGN)
    elif key=="convoy_wreck":
        chamfer(g,0,0,.235,3.10,1.35,.47,DARK,.26)
        g.rings([[(-1.30,-.58,.34),(.49,-.56,.34),(.19,.53,.34),(-1.5,.51,.34)],
                 [(-1.48,-.61,.72),(-.18,-.55,1.03),(.08,.50,.88),(-1.32,.45,.65)]],BODY)
        for y in (-.61,.61):
            g.tube((-.97,y,.27),(-.28,y*1.08,.22),.25,.20,TRIM,(12,9,6,6)[tier])
        if tier<2:
            for i in range(4):
                g.beam((-.12,.32-i*.21,.58),(1.05,.55-i*.30,.28+i*.09),.068,ARMOR)
            g.block(.62,-.25,.30,.76,.33,.27,OXIDE,.09)
            g.beam((-.58,-.55,.74),(-1.25,-.53,1.05),.091,ARMOR)
    elif key=="high_voltage_pillar":
        chamfer(g,0,0,.13,.85,.76,.26,STONE)
        g.profile(0,0,[(.26,.26),(.43,.22),(1.43,.14),(1.93,.136)],ARMOR,(14,10,7,6)[tier])
        for sign in (-1,1):
            n=8 if tier<2 else 5
            g.tube((0,sign*.08,1.42),(0,sign*.38,1.71),.071,.061,TRIM,n)
            bands=[(1.63,.057),(1.70,.115),(1.74,.07),(1.82,.12),(1.88,.065)]
            g.profile(0,sign*.38,bands if tier<2 else bands[::2],DARK,n)
        g.block(.22,0,.68,.083,.24,.32,AMBER)
    elif key=="market_booth":
        chamfer(g,-.12,0,.15,1.37,1.91,.29,STONE)
        for y in (-.87,.87):
            g.beam((-.46,y,.29),(-.41,y,2.02),.11,TRIM)
            g.beam((.49,y,.29),(.59,y,1.77),.10,TRIM)
        g.rings([[(-.82,-1.03,1.99),(.78,-1.03,1.75),(.78,1.03,1.75),(-.82,1.03,1.99)],
                 [(-.82,-1.03,2.10),(.78,-1.03,1.86),(.78,1.03,1.86),(-.82,1.03,2.10)]],BODY)
        chamfer(g,.34,0,.83,.66,1.91,.18,ARMOR)
        if tier<2:
            g.block(.65,0,1.77,.12,1.07,.19,SIGN)
    return g


def biome(key,tier):
    g=Form()
    g.section="Natural silhouette"
    if key.startswith("rock"):
        k=("rock_erma","rock_seira","rock_orren").index(key)
        r=Random(74+k)
        count=(14,10,7,6)[tier]
        height=(1.12,.76,1.36)[k]
        sides=[]
        for row,(z,rad) in enumerate(((.01,.60),(.27,.91),(.63,.79),(.93,.49),(1,.21))):
            sides.append([((rad+r.uniform(-.13,.13))*cos(i*tau/count)+row*.045,
                           (rad+r.uniform(-.11,.10))*sin(i*tau/count)*(.8+.14*k),z*height+r.uniform(-.045,.045))
                          for i in range(count)])
        g.rings(sides,(STONE,ARMOR,GLASS)[k])
        if tier<2:
            g.profile(.34,-.27,[(.02,.34),(.22,.35),(.46,.14),(.53,.064)],(DARK,AMBER,STONE)[k],(9 if tier==0 else 6))
    elif key.startswith("prism"):
        rng=Random(111 if key.endswith('a') else 216)
        g.profile(0,0,[(0,.52),(.17,.64),(.34,.51),(.38,.22)],DARK,9 if tier<2 else 6)
        for i in range((8,5,3,3)[tier]):
            a=i*2.4
            x,y=cos(a)*.32,sin(a)*.32
            high=.51+rng.random()*.75
            g.tube((x,y,.22),(x*1.57,y*1.57,high),.074+rng.random()*.08,.012,
                   LIGHT if key.endswith("a") and tier<3 else (GLASS if i%3 else ARMOR),6)
    else:
        k=("tree_erma","plant_seira","tree_orren").index(key)
        h=(3.40,1.28,2.45)[k]
        count=(7,4,3,3)[tier]
        trunk=[(0,.24),(h*.14,.19),(h*.52,.12),(h*.82,.067),(h,.025)]
        g.profile(0,0,trunk if tier<2 else trunk[::2],PLANT,(10,8,6,6)[tier])
        for i in range(count):
            a=i*2.40
            x,y=cos(a),sin(a)
            reach=(.78,.74,.58)[k]*(1-.38*i/count)
            z=h*(.36+.41*i/count)
            g.tube((0,0,z),(x*reach,y*reach,z+.12*h),.063,.025,PLANT,6 if tier<2 else 4)
            r=(.51,.32,.42)[k]
            leaf=[(z+.05*h,r*.24),(z+.11*h,r),(z+.31*h,r*.70),(z+.37*h,r*.07)]
            g.profile(x*reach,y*reach,leaf if tier<2 else [leaf[i] for i in (0,1,3)],
                      (FOLIAGE,OXIDE,GLASS)[k],(8,6,5,5)[tier],1.23 if k==0 else .83)
    return g


def prop(key,tier,group):
    if group=="nature":
        g=biome(key,tier)
    else:
        g=fixture(key,tier)
    if tier==3 and group!="nature":
        # Reconstruct from the same authored major forms with no small silhouette-independent hardware.
        g=fixture(key,2)
    return g
