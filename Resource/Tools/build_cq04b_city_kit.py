"""Author the CQ-04B meter-scale road, junction, grade separation and street kit."""
from collections import defaultdict
import json
from math import atan2, cos, pi, sin, tau
from pathlib import Path
import sys

import bpy
from mathutils import Vector
from mathutils.geometry import tessellate_polygon

ROOT = Path(__file__).resolve().parents[1]
TIERS = ("LOD0", "LOD1", "LOD2", "ShadowProxy")
ASPHALT, CONCRETE, CURB, STEEL, DARK = "asphalt", "paving", "curb", "steel", "dark"
WHITE, YELLOW, PLANT, GLASS, CYAN = "white", "yellow", "plant", "glass", "cyan"
ROAD_WIDTHS = {2: (6., 10.8), 4: (12.8, 17.6), 6: (19.4, 25.)}


class Mesh:
    def __init__(self):
        self.sections = defaultdict(list)
        self.section = "surface"

    def face(self, points, material):
        self.sections[self.section].append((tuple(points), material))

    def polygon(self, xy, z, mat):
        coords = [Vector((x, y, z)) for x, y in xy]
        for triangle in tessellate_polygon([coords]):
            pts = [coords[i] for i in triangle]
            if (pts[1]-pts[0]).cross(pts[2]-pts[0]).z<0:
                pts.reverse()
            self.face(pts, mat)

    def prism(self, xy, lo, hi, mat, top=None):
        if sum(a*d-b*c for (a,b),(c,d) in zip(xy,xy[1:]+xy[:1]))<0:
            xy=xy[::-1]
        self.polygon(xy,hi,top or mat)
        base=[Vector((x,y,lo)) for x,y in xy]
        for tri in tessellate_polygon([base]):
            points=[base[i] for i in tri]
            if (points[1]-points[0]).cross(points[2]-points[0]).z>0:
                points.reverse()
            self.face(points,mat)
        for (x,y),(u,v) in zip(xy,xy[1:]+xy[:1]):
            self.face(((x,y,lo),(u,v,lo),(u,v,hi),(x,y,hi)),mat)

    def box(self, x, y, z, sx, sy, sz, mat):
        xy=[(x-sx/2,y-sy/2),(x+sx/2,y-sy/2),(x+sx/2,y+sy/2),(x-sx/2,y+sy/2)]
        self.prism(xy,z-sz/2,z+sz/2,mat)

    def strip(self, rows, a, b, za, zb, mat):
        def value(lateral, x):
            return lateral(x) if callable(lateral) else lateral
        for (x, span, z),(xx, ss, zz) in zip(rows,rows[1:]):
            self.face(((x,value(a,x)*span,z+za),(xx,value(a,xx)*ss,zz+za),
                       (xx,value(b,xx)*ss,zz+zb),(x,value(b,x)*span,z+zb)),mat)


def rect(g, x, y, sx, sy, z, material):
    g.face(((x-sx/2,y-sy/2,z),(x+sx/2,y-sy/2,z),
            (x+sx/2,y+sy/2,z),(x-sx/2,y+sy/2,z)),material)


def stroke(g, a, b, width, z, material):
    x,y=a
    u,v=b
    d=((u-x)**2+(v-y)**2)**.5
    nx,ny=(y-v)*width/(2*d),(u-x)*width/(2*d)
    g.face(((x-nx,y-ny,z),(u-nx,v-ny,z),(u+nx,v+ny,z),(x+nx,y+ny,z)),material)


def swept(g, rows, width, height, outward, material):
    for sign in (-1,1):
        y=outward*sign
        if sign>0:
            g.strip(rows,y,y+width,height,height,material)
            g.strip(rows,y,y,0,height,material)
            g.strip(rows,y+width,y+width,height,0,material)
        else:
            g.strip(rows,y-width,y,height,height,material)
            g.strip(rows,y,y,height,0,material)
            g.strip(rows,y-width,y-width,0,height,material)


def road_section(g, rows, clear, median, sidewalks, lanes, lod, *, taper=False, rural=False, bay=None):
    def limits(x):
        return clear(x) if callable(clear) else clear
    def center(x):
        return median(x) if callable(median) else median
    def widen(x, side):
        return bay(x) if side<0 and bay else 0
    if sidewalks:
        g.strip(rows,lambda x:-limits(x)-widen(x,-1),limits,0,0,ASPHALT)
    else:
        g.strip(rows,-1,1,0,0,ASPHALT)
    if sidewalks:
        for sign in (-1,1):
            edge=lambda x: sign*(limits(x)+widen(x,sign))
            outside=lambda x:sign*(1+widen(x,sign))
            a,b=(outside,edge) if sign<0 else (edge,outside)
            g.strip(rows,a,b,.145,.145,CONCRETE)
            inner=lambda x:sign*(limits(x)+widen(x,sign)-.013)
            a,b=(edge,inner) if sign<0 else (inner,edge)
            g.strip(rows,a,b,.145 if sign<0 else .006,.006 if sign<0 else .145,CURB)
    if median:
        g.strip(rows,lambda x:-center(x),center,.145,.145,PLANT)
        for side in (-1,1):
            inner=lambda x:side*(center(x)-.006)
            outer=lambda x:side*(center(x)+.006)
            a,b=(outer,inner) if side<0 else (inner,outer)
            g.strip(rows,a,b,.15 if side>0 else 0,0 if side>0 else .15,CURB)
    if lod==3:
        return
    g.section="markings"
    if lanes==2 and not rural:
        g.strip(rows,-.008,.008,.004,.004,YELLOW)
    else:
        for sign in (-1,1):
            y=lambda x:sign*max(center(x)+.022,.020)
            g.strip(rows,lambda x:y(x)-.005,lambda x:y(x)+.005,.004,.004,YELLOW)
    for sign in (-1,1):
        y=lambda x:sign*(limits(x)+widen(x,sign)-.033)
        g.strip(rows,lambda x:y(x)-.006,lambda x:y(x)+.006,.004,.004,WHITE)
    if lod<=1:
        start,end=rows[0],rows[-1]
        for lane in range(1,lanes//2):
            for sign in (-1,1):
                edge=lambda x:sign*(center(x)+(limits(x)-center(x))*lane/(lanes/2))
                for i in range(2 if lod==1 else 4):
                    t=(i+.38)/(2 if lod==1 else 4)
                    q=t+.084
                    seg=[(start[0]+(end[0]-start[0])*w,start[1]+(end[1]-start[1])*w,
                          start[2]+(end[2]-start[2])*w) for w in (t,q)]
                    g.strip(seg,lambda x:edge(x)-.007,lambda x:edge(x)+.007,.004,.004,WHITE)
        if taper:
            length=rows[-1][0]
            for i in range(8 if lod==0 else 4):
                x=length*(.16+.085*i)
                f=1-x/length
                edge=(rows[-1][1]+f*(rows[0][1]-rows[-1][1]))*limits(x)
                for sign in (-1,1):
                    stroke(g,(x,sign*(edge-.75)),(x+1.9,sign*(edge-.20)),.11,.005,WHITE)
    if lod==0 and sidewalks:
        g.section="drains"
        for sign in (-1,1):
            for t in (.20,.80):
                a,b=rows[0],rows[-1]
                width=a[1]+t*(b[1]-a[1])
                x=a[0]+t*(b[0]-a[0])
                y=sign*(width*(limits(x)+widen(x,sign))-.18)
                g.box(x,y,.014,.50,.21,.020,DARK)
                for offset in (-.17,-.085,0,.085,.17):
                    rect(g,x+offset,y,.030,.19,.024,STEEL)


def street(g, lod, length, widths, lanes, clear, median=0, *, rural=False, taper=False, ring=False,
           roadside=None):
    start,end=widths
    rows=[(0,start/2,0),(length,end/2,0)]
    bay=None
    if roadside in ("turn","transit"):
        rows=[(x,start/2,0) for x in (0,1.4,5,11,length-1.4,length)]
        def bay(x):
            return max(0,min((x-1.4)/3.6,(length-1.4-x)/3.6,1))*3.0/(start/2)
    def along(values):
        a,b=values
        return lambda x: a+(b-a)*x/length
    road_section(g,rows,along(clear) if isinstance(clear,tuple) else clear,
                 along(median) if isinstance(median,tuple) else median,not rural,lanes,lod,
                 taper=taper,rural=rural,bay=bay)
    if rural:
        g.section="shoulders"
        for a,b in ((-1,-clear),(clear,1)):
            g.strip(rows,a,b,.009,.009,CONCRETE)
    if ring and lod<3:
        g.section="outer_curb_and_mounts"
        g.strip(rows,-1,-.988,.188,.188,CURB)
        if lod<=1:
            g.box(length*.50,-start*.473,.158,.38,.38,.075,STEEL)
    if roadside=="guardrail":
        g.section="guardrail"
        for sign in (-1,1):
            g.box(length/2,sign*start*.49,.64,length,.14,.19,STEEL)
            for i in range(1,5 if lod<=1 else 3):
                g.box(i*3.1,sign*start*.49,.31,.14,.19,.62,STEEL)
    elif roadside and lod<3:
        g.section="inset_bay"
        edge=-start*clear/2
        offset=edge-1.62
        for x in (5.3,8.3,11.3):
            stroke(g,(x-1.1,edge+.18),(x+.23,edge+.18),.11,.005,WHITE)
        for x in (6.2,9.8):
            if roadside=="turn":
                stroke(g,(x-.58,offset),(x+.55,offset),.12,.006,WHITE)
                g.polygon([(x+.55,offset-.23),(x+.92,offset),(x+.55,offset+.23)],.006,WHITE)
            else:
                rect(g,x,offset,.20,.91,.006,YELLOW)


def junction_dimensions(a, b):
    y,x=ROAD_WIDTHS[a][0]/2,ROAD_WIDTHS[b][0]/2
    ex,ey=min(14,max(x+4.4,6.9)),min(14,max(y+4.4,6.9))
    return x,y,ex,ey


def cross(g,lod,a,b,tee=False):
    x,y,ex,ey=junction_dimensions(a,b)
    r=2.15
    walk_x=ROAD_WIDTHS[b][1]/2-x
    walk_y=ROAD_WIDTHS[a][1]/2-y
    south=-y if tee else -y-r
    g.polygon([(-x-r,south),(x+r,south),(x+r,y+r),(-x-r,y+r)],0,ASPHALT)
    for sign in (-1,1):
        g.polygon([(sign*(x+r),-y),(sign*ex,-y),(sign*ex,y),(sign*(x+r),y)],0,ASPHALT)
        if sign>0 or not tee:
            g.polygon([(-x,sign*(y+r)),(x,sign*(y+r)),(x,sign*ey),(-x,sign*ey)],0,ASPHALT)
    steps=(8,5,3,2)[lod]
    g.section="rounded_corner_curb"
    for sx in (-1,1):
        for sy in (-1,1):
            if tee and sy<0:
                continue
            edge=[(sx*(x+r-r*cos(i*pi/(2*steps))),sy*(y+r-r*sin(i*pi/(2*steps)))) for i in range(steps+1)]
            walk=edge+[(sx*ex,sy*y),(sx*ex,sy*(y+walk_y)),(sx*(x+walk_x),sy*(y+walk_y)),
                       (sx*(x+walk_x),sy*ey),(sx*x,sy*ey)]
            g.polygon(walk,.145,CONCRETE)
            for (u,v),(p,q) in zip(edge,edge[1:]):
                pts=((u,v,.006),(p,q,.006),(p,q,.145),(u,v,.145))
                g.face(pts if sx*sy>0 else pts[::-1],CURB)
    if tee:
        g.polygon([(-ex,-y-walk_y),(ex,-y-walk_y),(ex,-y),(-ex,-y)],.145,CONCRETE)
    g.section="median_ends"
    for sign in (-1,1):
        if a==6:
            g.prism([(sign*ex,-.7),(sign*ex,.7),
                     (sign*(ex-1.25),.19),(sign*(ex-1.25),-.19)],0,.145,CURB,PLANT)
        if b==6 and (sign>0 or not tee):
            g.prism([(-.7,sign*ey),(.7,sign*ey),
                     (.19,sign*(ey-1.25)),(-.19,sign*(ey-1.25))],0,.145,CURB,PLANT)
    if lod<3:
        for sign in (-1,1):
            g.section="stop_line_paint"
            rect(g,sign*(ex-.70),-sign*y*.49,.20,y*.94,.004,WHITE)
            if lod<=1:
                g.section="crosswalk_paint"
                for i in range(-int(y/.87),int(y/.87)+1):
                    rect(g,sign*(ex-2.42),i*.87,1.91,.39,.006,WHITE)
            if sign<0 and tee:
                continue
            g.section="stop_line_paint"
            rect(g,sign*x*.49,sign*(ey-.70),x*.94,.20,.004,WHITE)
            if lod<=1:
                g.section="crosswalk_paint"
                for i in range(-int(x/.87),int(x/.87)+1):
                    rect(g,i*.87,sign*(ey-2.42),.39,1.91,.006,WHITE)
        if lod<=1:
            g.section="turn_guide_paint"
            for sign in (-1,1):
                for i in range(4):
                    t=(i+.12)*pi/10
                    s=t+.085
                    stroke(g,(sign*(.7+(x-.7)*cos(t)),(y-.9)*sin(t)),
                           (sign*(.7+(x-.7)*cos(s)),(y-.9)*sin(s)),.11,.005,WHITE)


def disk(r,n):
    return [(r*cos(tau*i/n),r*sin(tau*i/n)) for i in range(n)]


def roundabout(g,lod,outer,inner,lanes):
    n=(64,44,28,16)[lod]
    def ring(a,b,z,mat, dashes=False):
        left,right=disk(a,n),disk(b,n)
        for i in range(n):
            j=(i+1)%n
            if not dashes or i%4 in (0,1):
                g.face(((*left[i],z),(*right[i],z),(*right[j],z),(*left[j],z)),mat)
    ring(inner,outer,0,ASPHALT)
    g.section="center_island"
    g.prism(disk(inner,n),0,.21,CURB,PLANT)
    if lod<3:
        ring(inner+.21,inner+.33,.005,WHITE)
        if lanes==2:
            ring((inner+outer)/2-.07,(inner+outer)/2+.07,.006,WHITE,True)
    w,total=(value/2 for value in ROAD_WIDTHS[lanes*2])
    mouth=outer+.26
    end=outer+5.5
    for i in range(4):
        angle=i*pi/2
        def rotated(x,y):
            return x*cos(angle)-y*sin(angle),x*sin(angle)+y*cos(angle)
        start=(outer*outer-w*w)**.5-.18
        g.section="entry_ports"
        g.face(((*rotated(start,-w),-.004),(*rotated(mouth,-w),0),
                (*rotated(mouth,w),0),(*rotated(start,w),-.004)),ASPHALT)
        g.polygon([rotated(mouth,-w),rotated(end,-w),rotated(end,w),rotated(mouth,w)],0,ASPHALT)
        g.section="entry_sidewalks"
        for side in (-1,1):
            g.polygon([rotated(mouth,side*w),rotated(end,side*w),
                       rotated(end,side*total),rotated(mouth,side*total)],.145,CONCRETE)
            g.polygon([rotated(mouth,side*(w-.13)),rotated(end,side*(w-.13)),
                       rotated(end,side*w),rotated(mouth,side*w)],.012,CURB)
        g.section="splitter_islands"
        g.prism([rotated(outer+.55,0),rotated(outer+4.85,-.43),rotated(outer+4.4,.68)],
                0,.125,CURB)
        if lod<=1:
            g.section="yield_markings"
            for j in range(5 if lanes==1 else 9):
                a=(j+.24)*w/(5 if lanes==1 else 9)
                g.polygon([rotated(outer+.45,a),rotated(outer+.45,a+.30),
                           rotated(outer+.10,a+.15)],.006,WHITE)
    if lod<=1:
        g.section="island_lighting"
        g.box(0,0,1.63,.20,.20,2.86,STEEL)
        g.box(0,0,3.11,1.20,.34,.18,CYAN)
        if lanes==2:
            g.prism(disk(inner*.56,12),.21,.55,CONCRETE,PLANT)


def grade(g,lod,mode,length,width,rise):
    if mode=="up":
        rows=[(0,width/2,0),(length,width/2,rise)]
    elif mode=="down":
        segment=(16,10,6,4)[lod]
        def ease(t):
            a=.08
            if t<a:
                return t*t/(2*a*(1-a))
            if t>1-a:
                u=1-t
                return 1-u*u/(2*a*(1-a))
            return (t-a/2)/(1-a)
        steps=sorted({0,.08,.92,1,*[i/segment for i in range(1,segment)]})
        rows=[(t*length,width/2,rise*ease(t)) for t in steps]
    else:
        rows=[(0,width/2,rise),(length,width/2,rise)]
    g.section="deck" if mode in ("up","viaduct","bridge") else "channel"
    g.strip(rows,-1,1,0,0,ASPHALT)
    if mode in ("viaduct","bridge","up"):
        g.strip(rows,1,-1,-.4,-.4,CONCRETE)
        swept(g,rows,.015,.78,.97,CONCRETE)
        for sign in (-1,1):
            g.strip(rows,sign,sign,-.4,0,CONCRETE)
        g.section="pier"
        if mode=="viaduct":
            g.box(length*.50,0,4,2.31,2.48,8,CONCRETE)
            g.box(length*.50,0,7.62,2.65,width*.57,.70,CONCRETE)
        elif mode=="bridge":
            for x in (1.02,length-1.02):
                g.box(x,0,-1.25,2.04,width*.96,2.43,CONCRETE)
            g.box(length*.5,0,-1.85,1.45,2.36,3.12,CONCRETE)
        else:
            for x in (length*.29,length*.55,length*.81):
                h=rise*x/length-.4
                g.box(x,0,h/2,2.06,2.52,h,CONCRETE)
    if mode in ("down","culvert"):
        g.section="retaining_walls"
        height=4.6 if mode=="culvert" else .55
        for side in (-1,1):
            y=side*(1-.018)
            for (x,w,z),(u,ww,zz) in zip(rows,rows[1:]):
                top=0.32 if mode=="down" else z+height
                top_end=0.32 if mode=="down" else zz+height
                corners=((x,y*w,z),(u,y*ww,zz),(u,y*ww,top_end),(x,y*w,top))
                g.face(corners if side<0 else corners[::-1],CONCRETE)
                a,b=sorted((side*.982,side*1.017))
                g.strip([(x,w,top),(u,ww,top_end)],a,b,0,0,CURB)
                if lod<=1:
                    mark=((x,y*w-side*.01,z+.31),(u,y*ww-side*.01,zz+.31),
                          (u,y*ww-side*.01,zz+.36),(x,y*w-side*.01,z+.36))
                    g.face(mark if side<0 else mark[::-1],CYAN)
        if mode=="culvert":
            g.section="roof_slab"
            g.box(length/2,0,rise+4.6+.50,length,width*1.037,1,CONCRETE)
    if lod<3:
        g.section="deck" if mode in ("up","viaduct","bridge") else "channel"
        for a,b in ((-.012,-.003),(.003,.012),(-.96,-.95),(.95,.96)):
            g.strip(rows,a,b,.005,.005,YELLOW if a<.1 and a>-.1 else WHITE)
        if width>=14:
            for sign in (-1,1):
                for i in range(4 if lod==0 else 2):
                    a=(i+.18)/(4 if lod==0 else 2)
                    b=a+.085
                    if mode=="down":
                        z0,z1=rise*ease(a),rise*ease(b)
                    elif mode=="up":
                        z0,z1=rise*a,rise*b
                    else:
                        z0=z1=rise
                    g.strip([(a*length,width/2,z0),(b*length,width/2,z1)],
                            sign*.49-.008,sign*.49+.008,.007,.007,WHITE)
        if lod==0:
            for i in range(2,12,2):
                t=i/12
                x=t*length
                z=rise*(t if mode in ("up","down") else 1)
                if mode=="down":
                    z=rise*ease(t)
                for side in (-1,1):
                    if mode in ("up","viaduct","bridge"):
                        g.box(x,side*(width*.487),z+.79,.35,.08,.07,CYAN)
                    else:
                        g.box(x,side*(width*.447),z+.006,.36,.29,.013,STEEL)


def prop(g,lod,key):
    if key=="signal_gantry_4aspect":
        g.section="gantry_support"
        g.box(0,0,2.7,.35,.42,5.4,STEEL)
        g.box(0,3.04,5.66,.30,6.4,.29,STEEL)
        g.section="head_housing"
        for y in (2.94,5.60):
            g.box(-.21,y,5.67,.28,.43,1.07,DARK)
        for i,(role,mat) in enumerate((("red","red"),("amber","amber"),("through_green","green"),("left_arrow","green"))):
            target=5.99-.21*i
            g.section=role
            for y in (2.94,5.60):
                if role=="left_arrow":
                    g.face(((-.356,y-.025,target+.074),(-.356,y-.025,target-.074),(-.356,y-.12,target)),mat)
                    g.face(((-.356,y-.028,target+.022),(-.356,y+.101,target+.022),
                            (-.356,y+.101,target-.022),(-.356,y-.028,target-.022)),mat)
                else:
                    pts=[(-.356,y+.069*cos(i*tau/8),target+.069*sin(i*tau/8)) for i in range(8)]
                    for i in range(1,7):
                        g.face((pts[0],pts[i+1],pts[i]),mat)
        g.section="gantry_support"
        g.box(0,0,.11,.78,.78,.22,CONCRETE)
    elif key=="signal_pedestrian_pole":
        g.box(0,0,1.43,.15,.15,2.86,STEEL)
        g.box(-.09,0,2.91,.24,.27,.58,DARK)
        g.box(-.10,0,1.10,.16,.13,.19,STEEL)
        for i,(role,mat) in enumerate((("ped_stop","red"),("ped_walk","green"),("ped_flash","amber"))):
            g.section=role
            z=3.10-i*.17
            g.face(((-.212,-.087,z-.055),(-.212,-.087,z+.055),
                    (-.212,.087,z+.055),(-.212,.087,z-.055)),mat)
    elif key.startswith("sign_"):
        g.box(0,0,1.03,.11,.12,2.06,STEEL)
        if key.endswith("yield"):
            xy=[(-.33,2.43),(.33,2.43),(0,1.92)]
            g.face(tuple((-.071,y,z) for y,z in xy),WHITE)
            g.face(tuple((-.074,y*.72,2.23+(z-2.23)*.72) for y,z in xy),"red")
        else:
            g.box(-.03,0,2.21,.14,1.62,.60,STEEL)
            for sign in (-1,1):
                z=2.21+sign*.105
                for offset in (-.50,-.26,0,.26):
                    g.box(-.111,offset,z,.019,.19,.057,CYAN)
    elif key=="prop_streetlight_modern":
        g.prism(disk(.23,8 if lod==0 else 4),0,.21,CONCRETE)
        g.box(0,0,2.63,.16,.16,5.10,STEEL)
        g.box(0,.45,5.22,.18,1.10,.16,STEEL)
        g.box(0,.95,5.34,.38,.72,.18,DARK)
        g.box(0,.95,5.24,.28,.56,.026,CYAN)
    elif key=="prop_transit_shelter":
        for x in (-2.81,2.81):
            for y in (-.99,.99):
                g.box(x,y,1.30,.12,.13,2.60,STEEL)
            g.box(x,0,2.65,.14,2.40,.18,STEEL)
        g.box(0,0,2.71,5.94,2.37,.11,GLASS)
        g.box(0,-1.04,1.51,5.45,.04,1.80,GLASS)
        if lod<3:
            g.box(0,-.66,.53,3.88,.43,.14,STEEL)
            g.box(2.23,-.89,1.50,1.0,.07,.52,CYAN)
            for x in (-1.46,1.46):
                g.box(x,-.66,.27,.15,.18,.52,STEEL)
    elif key=="prop_plaza_planter":
        p=disk(.84,(12,8,6,5)[lod])
        g.prism(p,0,.37,CURB,PLANT)
        if lod<3:
            g.prism(disk(.67,(10,7,5,4)[lod]),.36,.64,PLANT)
    elif key=="prop_bench":
        g.box(0,0,.51,1.95,.53,.11,STEEL)
        g.box(0,-.22,.76,1.95,.10,.50,STEEL)
        for x in (-.74,.74):
            g.box(x,0,.26,.18,.49,.52,CURB)
    elif key=="prop_bollard":
        g.prism(disk(.135,8 if lod<2 else 5),0,.77,STEEL)
        if lod<3:
            g.prism(disk(.14,8 if lod<2 else 5),.69,.76,CYAN)
    else:
        g.box(4,4,-.045,8,8,.09,CONCRETE)
        if lod<2:
            for n in (2,4,6):
                rect(g,n,4,.032,7.97,.003,CURB)
                rect(g,4,n,7.97,.032,.005,CURB)


def recipes():
    out={}
    def add(key,cap,fn,*args,**kwargs):
        out[key]=(cap,fn,args,kwargs)
    add("road_local_2lane_16m",600,street,16,(10.8,10.8),2,6/10.8)
    add("road_collector_4lane_16m",900,street,16,(17.6,17.6),4,12.8/17.6)
    add("road_arterial_6lane_16m",1400,street,16,(25,25),6,19.4/25,1.4/25)
    add("road_taper_6to4_32m",1200,street,32,(25,17.6),6,(19.4/25,12.8/17.6),(1.4/25,0),taper=True)
    add("road_taper_4to2_24m",1200,street,24,(17.6,10.8),4,(12.8/17.6,6/10.8),taper=True)
    add("road_highway_regional_16m",700,street,16,(10.7,10.7),2,9/10.7,rural=True)
    add("road_ring_4lane_16m",900,street,16,(17.6,17.6),4,12.8/17.6,ring=True)
    add("road_arterial_6lane_16m_turn_bay",1400,street,16,(25,25),6,19.4/25,1.4/25,roadside="turn")
    add("road_arterial_6lane_16m_transit_bay",1400,street,16,(25,25),6,19.4/25,1.4/25,roadside="transit")
    add("road_highway_regional_16m_guardrail",700,street,16,(10.7,10.7),2,9/10.7,rural=True,roadside="guardrail")
    for a,b in ((2,2),(4,2),(4,4),(6,4),(6,6)):
        add(f"junc_cross_{a}x{b}",1500,cross,a,b)
    for a,b in ((2,2),(4,2)):
        add(f"junc_tee_{a}x{b}",1100,cross,a,b,True)
    add("junc_roundabout_1lane",2200,roundabout,13,7,1)
    add("junc_roundabout_2lane",3200,roundabout,20.5,11,2)
    add("elevated_viaduct_4lane_16m",2000,grade,"viaduct",16,14.4,8.4)
    add("elevated_ramp_4lane_100m",2400,grade,"up",100,14.4,8.4)
    add("underpass_ramp_4lane_80m",2000,grade,"down",80,14.4,-5.4)
    add("underpass_box_culvert_32m",2200,grade,"culvert",32,14.4,-5.4)
    add("bridge_4lane_32m",1800,grade,"bridge",32,14.4,0)
    add("bridge_2lane_32m",1800,grade,"bridge",32,8.3,0)
    props={
        "signal_gantry_4aspect":1200,"signal_pedestrian_pole":450,"sign_roundabout_yield":350,
        "sign_portal_wayfinding":350,"prop_streetlight_modern":350,"prop_transit_shelter":900,
        "prop_plaza_planter":300,"prop_bench":300,"prop_bollard":300,"plaza_tile_8m":64,
    }
    for key,cap in props.items():
        add(key,cap,prop,key)
    return out


def port(x, y, z, dx, dy, width, pavement=None):
    result={"point_m":[x,y,z],"outward":[dx,dy,0],"width_m":width}
    if pavement is not None:
        result["paved_width_m"]=pavement
    return result


def ports_for(key, maker, args, options):
    if maker is street:
        length,widths,lanes,*_=args
        if options.get("rural"):
            return [port(0,0,0,-1,0,widths[0]),port(length,0,0,1,0,widths[1])]
        final=4 if "6to4" in key else 2 if "4to2" in key else lanes
        return [port(0,0,0,-1,0,widths[0],ROAD_WIDTHS[lanes][0]),
                port(length,0,0,1,0,widths[1],ROAD_WIDTHS[final][0])]
    if maker is grade:
        mode,length,width,rise=args
        start=0 if mode in ("up","down") else rise
        return [port(0,0,start,-1,0,width),port(length,0,rise,1,0,width)]
    if maker is cross:
        a,b,*mode=args
        _,_,ex,ey=junction_dimensions(a,b)
        p,w=ROAD_WIDTHS[a]
        q,h=ROAD_WIDTHS[b]
        ports=[port(-ex,0,0,-1,0,w,p),port(ex,0,0,1,0,w,p),port(0,ey,0,0,1,h,q)]
        if not mode:
            ports.append(port(0,-ey,0,0,-1,h,q))
        return ports
    if maker is roundabout:
        outer,_,lanes=args
        paved,width=ROAD_WIDTHS[lanes*2]
        end=outer+5.5
        return [port(end*x,end*y,0,x,y,width,paved) for x,y in ((1,0),(0,1),(-1,0),(0,-1))]
    return []


def verify_ports(key, lod, geom, ports):
    for entry in ports:
        px,py,pz=entry["point_m"]
        dx,dy,_=entry["outward"]
        pavement,sidewalk=[],[]
        for section,faces in geom.sections.items():
            for points,material in faces:
                for x,y,z in points:
                    if abs((x-px)*dx+(y-py)*dy)>1e-6:
                        continue
                    offset=(y-py)*dx-(x-px)*dy
                    if material==ASPHALT and abs(z-pz)<1e-6:
                        pavement.append(offset)
                    if material==CONCRETE and abs(z-pz-.145)<1e-6 and section in (
                            "surface","rounded_corner_curb","entry_sidewalks"):
                        sidewalk.append(offset)
        declared=entry.get("paved_width_m",entry["width_m"])
        assert pavement and abs(min(pavement)+declared/2)<1e-6 and abs(max(pavement)-declared/2)<1e-6, (
            key,lod,entry,pavement)
        if "paved_width_m" in entry:
            assert sidewalk and abs(min(sidewalk)+entry["width_m"]/2)<1e-6 and abs(
                max(sidewalk)-entry["width_m"]/2)<1e-6,(key,lod,entry,sidewalk)


def materials():
    def material(name, rgb, metal=0, rough=.78, tile=None, emissive=False, glass=False):
        data=bpy.data.materials.new(name)
        data.diffuse_color=(*rgb,1)
        data.use_nodes=True
        nodes=data.node_tree.nodes
        links=data.node_tree.links
        bsdf=nodes["Principled BSDF"]
        bsdf.inputs["Base Color"].default_value=(*rgb,1)
        bsdf.inputs["Metallic"].default_value=metal
        bsdf.inputs["Roughness"].default_value=rough
        if tile:
            maps={}
            for label in ("BaseColor","Normal","ORM"):
                node=nodes.new("ShaderNodeTexImage")
                img=ROOT/"Export/Textures"/f"{tile}_{label}.png"
                node.image=bpy.data.images.load(str(img),check_existing=True)
                node.image.colorspace_settings.name="sRGB" if label=="BaseColor" else "Non-Color"
                maps[label]=node.outputs["Color"]
            color=nodes.new("ShaderNodeMixRGB")
            color.blend_type="MULTIPLY"
            color.inputs[0].default_value=1
            color.inputs[1].default_value=(*rgb,1)
            links.new(maps["BaseColor"],color.inputs[2])
            links.new(color.outputs[0],bsdf.inputs["Base Color"])
            normal=nodes.new("ShaderNodeNormalMap")
            normal.inputs["Strength"].default_value=.62
            links.new(maps["Normal"],normal.inputs["Color"])
            links.new(normal.outputs[0],bsdf.inputs["Normal"])
            orm=nodes.new("ShaderNodeSeparateColor")
            links.new(maps["ORM"],orm.inputs[0])
            links.new(orm.outputs[1],bsdf.inputs["Roughness"])
            links.new(orm.outputs[2],bsdf.inputs["Metallic"])
            group=bpy.data.node_groups.get("glTF Material Output")
            if group is None:
                group=bpy.data.node_groups.new("glTF Material Output","ShaderNodeTree")
                group.interface.new_socket(name="Occlusion",in_out="INPUT",socket_type="NodeSocketFloat")
            occlusion=nodes.new("ShaderNodeGroup")
            occlusion.node_tree=group
            links.new(orm.outputs[0],occlusion.inputs["Occlusion"])
        if emissive:
            bsdf.inputs["Emission Color"].default_value=(*rgb,1)
            bsdf.inputs["Emission Strength"].default_value=1.7
        if glass:
            bsdf.inputs["Alpha"].default_value=.28
            data.surface_render_method="BLENDED"
        return data
    return {
        ASPHALT:material("Road Basalt",(.65,.73,.83),tile="Road_Basalt"),
        CONCRETE:material("Concrete Paving",(.67,.72,.72),tile="Habitat_Concrete"),
        CURB:material("Cast Curb",(.45,.51,.52),tile="Habitat_Concrete"),
        STEEL:material("Satin Steel",(.22,.29,.32),.78,.38),
        DARK:material("Carbon Housing",(.029,.049,.062),.13,.46),
        WHITE:material("Reflective Ceramic Paint",(.80,.86,.82),0,.42),
        YELLOW:material("Amber Lane Paint",(.95,.63,.10),0,.50),
        PLANT:material("Dryland Planting",(.22,.30,.15),tile="Terrain_AlienGrass"),
        GLASS:material("Glass Canopy",(.34,.71,.80),.08,.29,glass=True),
        CYAN:material("Cyan Emissive",(.13,.81,.88),emissive=True),
        "red":material("Red Emissive",(.95,.063,.07),emissive=True),
        "amber":material("Amber Emissive",(.99,.56,.057),emissive=True),
        "green":material("Green Emissive",(.13,.95,.56),emissive=True),
    }


def objects(g,group,mats):
    result=[]
    for name,faces in g.sections.items():
        if not faces:
            continue
        positions,indices,face_materials=[],[],[]
        weld={}
        for pts,kind in faces:
            ids=[]
            for point in pts:
                point=tuple(round(c,7) for c in point)
                if point not in weld:
                    weld[point]=len(positions)
                    positions.append(point)
                ids.append(weld[point])
            ids=list(dict.fromkeys(ids))
            if len(ids)<3:
                continue
            indices.append(ids)
            face_materials.append(kind)
        if not indices:
            continue
        data=bpy.data.meshes.new(group.name+"_"+name)
        data.from_pydata(positions,[],indices)
        data.update()
        assert not data.validate(), (group.name,name)
        kinds=list(dict.fromkeys(face_materials))
        for k in kinds:
            data.materials.append(mats[k])
        uv=data.uv_layers.new(name="UVMap")
        for face,key in zip(data.polygons,face_materials):
            face.material_index=kinds.index(key)
            axis=max(range(3),key=lambda i:abs(face.normal[i]))
            a,b=((1,2),(2,0),(0,1))[axis]
            sign=1 if face.normal[axis]>0 else -1
            for loop in face.loop_indices:
                p=data.vertices[data.loops[loop].vertex_index].co
                uv.data[loop].uv=(sign*p[a]/2,p[b]/2)
        data.calc_loop_triangles()
        assert all(poly.area>1e-9 for poly in data.polygons),(group.name,name)
        obj=bpy.data.objects.new(name,data)
        obj["asset_id"]=name
        group.objects.link(obj)
        result.append(obj)
    return result


def review(scene, source, target, ports=None):
    def camera(name,point,focus,scale):
        data=bpy.data.cameras.new(name)
        o=bpy.data.objects.new(name,data)
        scene.collection.objects.link(o)
        o.location=point
        o.rotation_euler=(Vector(focus)-o.location).to_track_quat("-Z","Y").to_euler()
        data.type="ORTHO"
        data.ortho_scale=scale
        scene.camera=o
    if ports is None:
        sources=(("junc_roundabout_1lane",(-25,9,0),0),("junc_cross_4x4",(15,10,0),0),
                 ("elevated_viaduct_4lane_16m",(-32,-18,0),0),("bridge_2lane_32m",(5,-24,0),0),
                 ("prop_transit_shelter",(17,-16,0),0),("signal_gantry_4aspect",(28,-15,0),0))
        camera("City kit preview",(65,-91,102),(0,0,0),105)
        filename="CQ04B_City_Kit.png"
    else:
        approaches=[]
        for entry in ports:
            key="road_arterial_6lane_16m" if entry["width_m"]==25 else "road_collector_4lane_16m"
            x,y,_=entry["outward"]
            approaches.append((key,entry["point_m"],atan2(y,x)))
        sources=[("junc_cross_6x4",(0,0,0),0),*approaches]
        camera("Road port assembly",(32,-46,77),(0,0,0),77)
        filename="CQ04B_Port_Fit.png"
    for key,location,yaw in sources:
        for original in source[key]:
            copy=original.copy()
            scene.collection.objects.link(copy)
            copy.location=location
            copy.rotation_euler.z=yaw
    world=bpy.data.worlds.new("City studio")
    world.use_nodes=True
    world.node_tree.nodes["Background"].inputs[0].default_value=(.20,.24,.31,1)
    world.node_tree.nodes["Background"].inputs[1].default_value=.55
    scene.world=world
    data=bpy.data.lights.new("Sun","SUN")
    data.energy=2.4
    o=bpy.data.objects.new("Sun",data)
    scene.collection.objects.link(o)
    o.rotation_euler=(.55,-.41,-.33)
    scene.render.engine="CYCLES"
    scene.cycles.samples=16
    scene.cycles.use_denoising=True
    scene.render.resolution_x,scene.render.resolution_y=1440,900
    scene.render.resolution_percentage=100
    scene.render.filepath=str(target/filename)
    bpy.ops.render.render(write_still=True)
    scene.render.filepath=f"//../Preview/{filename}"


def main():
    output=Path(sys.argv[sys.argv.index("--")+1]).resolve()
    output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    library=bpy.context.scene
    library.name="CQ-04B | asset library"
    library.unit_settings.system="METRIC"
    style=materials()
    records={}
    first={}
    for key,(cap,maker,args,kwargs) in recipes().items():
        lods={}
        ports=ports_for(key,maker,args,kwargs)
        for index,tier in enumerate(TIERS):
            geom=Mesh()
            maker(geom,index,*args,**kwargs)
            verify_ports(key,tier,geom,ports)
            group=bpy.data.collections.new(key+"_"+tier)
            library.collection.children.link(group)
            meshes=objects(geom,group,style)
            tri_count=sum(len(o.data.loop_triangles) for o in meshes)
            assert 0<tri_count<=cap,(key,tier,tri_count,cap)
            if key=="prop_streetlight_modern" and index==1:
                assert tri_count<=120
            bpy.ops.object.select_all(action="DESELECT")
            sections=[o["asset_id"] for o in meshes]
            for obj in meshes:
                obj.name=key+"__"+obj["asset_id"]
                obj.select_set(True)
            filename=f"{key}_{tier}.glb"
            bpy.ops.export_scene.gltf(filepath=str(output/filename),export_format="GLB",
                use_selection=True,export_animations=False,export_tangents=True,export_extras=True)
            points=[v.co for o in meshes for v in o.data.vertices]
            lods[tier]={"file":filename,"triangles":tri_count,"sections":sections,
                        "bounds_m":[[round(f(p[i] for p in points),5) for i in range(3)] for f in (min,max)]}
            if index==0:
                first[key]=meshes
            group.hide_viewport=True
        records[key]={"lod0_cap":cap,"lods":lods}
        if ports:
            records[key]["ports"]=ports
        if maker is street:
            lanes=args[2]
            records[key]["lanes_at_start"]=lanes
            records[key]["lanes_at_end"]=4 if "6to4" in key else 2 if "4to2" in key else lanes
    assert len(records)==35
    fixed={
        "elevated_viaduct_4lane_16m":{"deck_clearance_m":8.0,"deck_height_m":8.4,"fade_section":"deck","vertical_collision_section":"pier"},
        "elevated_ramp_4lane_100m":{"length_m":100,"rise_m":8.4,"max_grade":.084},
        "underpass_ramp_4lane_80m":{"length_m":80,"rise_m":-5.4,"max_grade":.054/.736},
        "underpass_box_culvert_32m":{"clearance_m":4.6,"road_level_m":-5.4,"roof_thickness_m":1.0,"fade_section":"roof_slab"},
        "signal_gantry_4aspect":{"right_turn_socket_m":[-.20,6.08,5.67],"support_section":"gantry_support",
                                 "housing_section":"head_housing",
                                 "aspect_sections":["red","amber","through_green","left_arrow"]},
        "signal_pedestrian_pole":{"aspect_sections":["ped_stop","ped_walk","ped_flash"]},
        "road_ring_4lane_16m":{"streetlight_socket_m":[8,-8.3248,.20]},
    }
    for key,meta in fixed.items():
        records[key].update(meta)
    fit=bpy.data.scenes.new("CQ-04B | 6x4 port fit")
    bpy.context.window.scene=fit
    review(fit,first,output,ports=records["junc_cross_6x4"]["ports"])
    scene=bpy.data.scenes.new("CQ-04B | review")
    bpy.context.window.scene=scene
    review(scene,first,output)
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(output/"CQ04B_City_Kit.blend"),compress=True)
    manifest={"schema":"cq04b_city_kit_v4","frame":{"unit":"metre","forward":"+X","left":"+Y","up":"+Z",
              "baked_runtime_road_offset_m":0},"assets":records}
    (output/"cq04b_city_kit_v4.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf8")
    print("CQ04B",len(records))


if __name__=="__main__":
    main()
