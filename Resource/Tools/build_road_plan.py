"""Author ROAD_MODULE_REQUEST P0/P1 assets; never change game code or Runtime.

blender --background --python Resource/Tools/build_road_plan.py
Use -- --geometry-check for a non-destructive draft gate.
"""
import ast
import hashlib
import json
import math
import sys
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector,Matrix,Quaternion

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'Source/Erma_Resource_Library.blend'
TAG='road_plan_v1'
REVISION='road_first_city_plan_2026_10_23'
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
entry_scene=bpy.context.scene.name
bpy.context.preferences.filepaths.save_version=0
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))

def protected_hash():
    digest=hashlib.sha256()
    for a in sorted(catalog['assets'],key=lambda a:a['id']):
        if a.get('expansion')==TAG:continue
        o=bpy.data.objects[a['id']]
        digest.update(a['id'].encode())
        digest.update(json.dumps([[tuple(v.co) for v in o.data.vertices],
                                  [tuple(p.vertices) for p in o.data.polygons],
                                  [m.name for m in o.data.materials]],separators=(',',':')).encode())
    return digest.hexdigest()

protected_before=protected_hash()
for s in list(bpy.data.scenes):
    if s.get('expansion')==TAG:bpy.data.scenes.remove(s)
for o in list(bpy.data.objects):
    if o.get('expansion')==TAG:bpy.data.objects.remove(o,do_unlink=True)
for c in list(bpy.data.collections):
    if c.get('expansion')==TAG:bpy.data.collections.remove(c)
scene=bpy.data.scenes['00_Library'];bpy.context.window.scene=scene;scene.frame_set(0)
MATS={m.name:m for m in bpy.data.materials}
for name,source,color,rough,metal,glow in [
    ('Road_Asphalt',None,(.075,.10,.115),.93,.02,0),
    ('Road_Edge',None,(.21,.29,.30),.62,.42,0),
    ('Walk_Paving',None,(.34,.43,.42),.85,.06,0),
    ('Marking_White',None,(.77,.82,.75),.70,.02,0),
    ('Marking_Center',None,(.72,.43,.105),.68,.02,0),
    ('Barrier_Metal',None,(.27,.36,.38),.37,.73,0),
    ('Structure_Concrete',None,(.29,.34,.34),.88,.08,0),
    ('Signal_Emission',None,(.035,.57,.66),.35,.25,1.25)]:
    m=MATS.get(name) or bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1)
    shader=m.node_tree.nodes.get('Principled BSDF')
    for k,v in {'Base Color':(*color,1),'Roughness':rough,'Metallic':metal,
                'Emission Color':(*color,1),'Emission Strength':glow}.items():shader.inputs[k].default_value=v
    MATS[name]=m

# Import authoring implementations without running other builders.
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='Geo')],type_ignores=[]),'<base geometry>','exec'))
LegacyGeo=Geo
sys.path.insert(0,str(ROOT/'Tools'))
from detail_mesh_pipeline import DetailGeometry,DetailPipeline
from road_plan_tangent_export import repair as repair_tangents
from road_plan_source import write_project,style_elevation

class Geo(DetailGeometry,LegacyGeo):
    def box(self,c,s,mat,bone=None,rot=None):
        if mat not in {'Barrier_Metal','Structure_Concrete','Road_Edge'} or min(s)<.045 or max(s)<.25:
            return LegacyGeo.box(self,c,s,mat,bone,rot)
        bm=bmesh.new();bmesh.ops.create_cube(bm,size=1)
        for v in bm.verts:v.co=Vector((v.co.x*s[0],v.co.y*s[1],v.co.z*s[2]))
        bmesh.ops.bevel(bm,geom=list(bm.edges),offset=min(.012,min(s)*.10),segments=1,affect='EDGES',clamp_overlap=True)
        bm.verts.ensure_lookup_table();bm.verts.index_update()
        self.add([(rot@v.co if rot else v.co)+Vector(c) for v in bm.verts],
                 [tuple(v.index for v in f.verts) for f in bm.faces],mat,bone)
        bm.free();return self

LIB=bpy.data.collections.new('20_RoadPlan_Modules');LIB['expansion']=TAG
bpy.data.collections['RESOURCE_LIBRARY'].children.link(LIB)
rawscene=bpy.data.scenes.new('36_RoadPlan_Detail_Sources');rawscene['expansion']=TAG
rawscene.unit_settings.system='METRIC'
rawcol=bpy.data.collections.new('RoadPlan_Editable_Sources');rawcol['expansion']=TAG
rawscene.collection.children.link(rawcol)
PIPE=DetailPipeline(rawcol,MATS,TAG,allow_draft_over_budget=True)
ASSETS=[];PROTOTYPES={};MODULES={};EXPORTS=[];EXAMPLES={};TANGENT_REPAIRS={}

def port(pid,p,d,width,lanes=0,level=0,kind='road',slope=0,**extra):
    d=Vector(d).normalized()
    return dict(id=pid,kind=kind,position_m=list(p),outward=list(d),width_m=width,
                lanes=lanes,level=level,slope=slope,**extra)

def bounds(objects):
    points=[v.co for o in objects for v in o.data.vertices]
    return dict(min=[min(v[k] for v in points) for k in range(3)],max=[max(v[k] for v in points) for k in range(3)])

def module(name,specs,ports=None,category='road',priority='P0',**meta):
    name=name if name.startswith('RoadPlan_') else 'RoadPlan_'+name
    c=bpy.data.collections.new(name+'_Parts');c['expansion']=TAG;LIB.children.link(c)
    objects=[];parts=[];collisions=[]
    for label,g,role in specs:
        if not g.f:continue
        pid=name+'_'+label
        ob,a=PIPE.build(g,pid,c,None,label,'road_plan.'+category)
        PIPE.records[-1]['optimized_object']=ob.name
        ob['expansion']=TAG;ob['collision_role']=role;ob['module_id']=name;ob['authoring_up']='+Z'
        a.update(expansion=TAG,module_id=name,collision_role=role,blender_object=ob.name)
        ASSETS.append(a);PROTOTYPES[pid]=ob;objects.append(ob);parts.append(pid)
        if role!='none':collisions.append(dict(part=pid,role=role,bounds=bounds([ob]),shape='actual_mesh_or_convex_component_hulls'))
    info=dict(parts=parts,category=category,priority=priority,ports=ports or [],bounds=bounds(objects),
              road_surface_z_m=0,walk_surface_z_m=.15,scale=[1,1,1],
              collision_envelope=collisions,companions=[],**meta)
    MODULES[name]=info
    return name

def sample(p,d=(1,0),z=0,slope=0):
    return dict(position_m=[p[0],p[1],z],tangent=list(Vector((d[0],d[1],slope)).normalized()),
                direction_xy=list(d),slope=slope)

def linear(length,centered=True,height=0,slope=0,steps=1):
    start=-length/2 if centered else 0
    return [sample((start+length*i/steps,0),z=height+slope*length*i/steps,slope=slope) for i in range(steps+1)]

def sweep(path,width,mat,z=0,depth=.20,offset=0,g=None):
    g=g or Geo();verts=[]
    for i,q in enumerate(path):
        x,y,zz=q['position_m'];dx,dy=q['direction_xy']
        w=width(i/(len(path)-1)) if callable(width) else width
        off=offset(i/(len(path)-1)) if callable(offset) else offset
        for side,h in [(-1,-depth),(1,-depth),(1,0),(-1,0)]:
            lateral=off+side*w/2;verts.append((x-dy*lateral,y+dx*lateral,zz+z+h))
    faces=[(3,2,1,0)]
    for i in range(len(path)-1):
        for k in range(4):faces.append((i*4+k,(i+1)*4+k,(i+1)*4+(k+1)%4,i*4+(k+1)%4))
    j=(len(path)-1)*4;faces.append((j,j+1,j+2,j+3))
    return g.add(verts,faces,mat)

def endpoints(path,width,lanes=0,level=0,kind='road',walk=False):
    result=[]
    for i,pid,sign in [(0,'start',-1),(-1,'end',1)]:
        q=path[i];pos=list(q['position_m']);pos[2]+=.15 if walk else 0
        w=width(0 if i==0 else 1) if callable(width) else width
        result.append(port(pid,pos,[sign*x for x in q['tangent']],w,lanes,level,kind,q['slope']))
    return result

def outline(path,width,offset=0):
    sides=[]
    for side in [-1,1]:
        points=[]
        for i,q in enumerate(path):
            t=i/(len(path)-1);w=width(t) if callable(width) else width;off=offset(t) if callable(offset) else offset
            x,y,z=q['position_m'];dx,dy=q['direction_xy'];lateral=off+side*w/2
            points.append([x-dy*lateral,y+dx*lateral,z])
        sides.append(points)
    return sides[0]+list(reversed(sides[1]))

def marking_specs(path,width,lanes,prefix=''):
    specs=[(prefix+'Center',sweep(path,.085,'Marking_Center',.003,.0007),'none')]
    for side in [-1,1]:
        specs.append((prefix+('EdgeLeft' if side==1 else 'EdgeRight'),sweep(path,.075,'Marking_White',.003,.0007,side*(width/2-.15)),'none'))
    if lanes==4:
        count=max(1,int(abs(path[-1]['position_m'][0]-path[0]['position_m'][0])/4))
        for side in [-1,1]:
            g=Geo()
            # Sample the full path for curves, using each selected interval as a lane dash.
            for k in range(0,len(path)-1,2):sweep(path[k:k+2],.075,'Marking_White',.003,.0007,side*3,g)
            specs.append((prefix+('LaneLeft' if side==1 else 'LaneRight'),g,'none'))
    return specs

def path_road(name,path,lanes,category='road',**extra):
    width=lanes*3
    name=module(name,[('Surface',sweep(path,width,'Road_Asphalt'), 'drive_surface')],
                endpoints(path,width,lanes,1 if category=='deck' else 0),category,
                lanes=lanes,road_outline=outline(path,width),centerline_samples=path,
                uv_policy='continuous periodic path coordinates; integer endpoint phase; signed lateral / 2m',**extra)
    marks=module(name.replace('RoadPlan_Surface_','RoadPlan_Marking_').replace('RoadPlan_Curve_','RoadPlan_CurveMarking_').replace('RoadPlan_Deck_','RoadPlan_DeckMarking_'),
                 marking_specs(path,width,lanes),category='marking',paint_only=True,road_companion=name)
    MODULES[name]['companions'].append(dict(module=marks,position_m=[0,0,0],yaw_degrees=0))
    return name

for lanes in [2,4]:
    for length in [4,8,16]:path_road(f'Surface_{lanes}L_L{length}',linear(length,steps=max(1,length//2)),lanes,length_m=length)

def walk(name,length,width=2,endcap=False):
    path=linear(length)
    g=Geo()
    for x in range(int(-length/2)+1,int(length/2)+1,2):g.box((x,0,.1508),(.012,width-.04,.0016),'Road_Edge')
    specs=[('Paving',sweep(path,width,'Walk_Paving',.15,.30),'walk_surface')]
    if g.f:specs.append(('PanelJoints',g,'none'))
    if endcap:specs.append(('EndTrim',Geo().box((length/2-.02,0,-.005),(.04,width,.31),'Road_Edge'),'walk_edge'))
    ports=endpoints(path,width,kind='walk',walk=True)
    if endcap:ports=ports[:1]
    return module(name,specs,ports,'walk',effective_walk_width_m=width,length_m=length,
                  walk_outline=[[p[0],p[1],p[2]+.15] for p in outline(path,width)],edge_trim_is_flush=True)

for length in [2,4,8]:walk(f'Walk_W2_L{length}',length)
walk('Walk_W2_EndCap_L1',1,endcap=True)
walk('WalkLanding_W4_L2',2,4)

# Curves preserve the R7 life-street centerline; tiny endpoint stations also
# keep actual mesh chord tangents within 0.1 degree at connections.
CURVES=[]
for radius,angle in [(7,90)]+[(r,a) for r in [18,36] for a in [15,30,45]]:
    angles=sorted(set([0,.05,angle-.05,angle]+[angle*i/math.ceil(angle/2.5) for i in range(1,math.ceil(angle/2.5))]))
    path=[sample((radius*math.sin(math.radians(a)),radius*(1-math.cos(math.radians(a)))),
                 (math.cos(math.radians(a)),math.sin(math.radians(a)))) for a in angles]
    name=path_road(f'Curve_2L_A{angle}_R{radius}',path,2,radius_m=radius,turn_degrees=angle,
                   path_length_m=radius*math.radians(angle),reverse_allowed=True)
    CURVES.append(name)

def walk_sides(name,path,width,roadname):
    specs=[];ports=[]
    for side in [-1,1]:
        off=(lambda t,side=side:side*((width(t) if callable(width) else width)/2+.12+1))
        curb=(lambda t,side=side:side*((width(t) if callable(width) else width)/2+.06))
        specs.append((f'Paving_{side}',sweep(path,2,'Walk_Paving',.15,.30,off),'walk_surface'))
        specs.append((f'Curb_{side}',sweep(path,.12,'Road_Edge',.15,.30,curb),'walk_edge'))
        for index,pid,sign in [(0,'start',-1),(-1,'end',1)]:
            q=path[index];x,y,z=q['position_m'];dx,dy=q['direction_xy'];offset=off(0 if index==0 else 1)
            ports.append(port(f'{side}_{pid}',[x-dy*offset,y+dx*offset,z+.15],
                              [sign*dx,sign*dy,0],2,kind='walk',role='pedestrian_route'))
    wn=module(name,specs,ports,'walk','P1',road_companion=roadname,effective_walk_width_m=2,
              curb_width_m=.12,inner_walk_width_m=2,road_edge_samples=outline(path,width))
    MODULES[roadname]['companions'].append(dict(module=wn,position_m=[0,0,0],yaw_degrees=0))
    return wn

for name in CURVES:
    walk_sides(name.replace('Curve_','WalkCurve_'),MODULES[name]['centerline_samples'],6,name)

# T4+4+2 and T2/X2 use one concave outline, never overlapping rectangles.
def slab(points,z,depth,mat):
    n=len(points)
    return Geo().add([(x,y,h) for h in [z-depth,z] for x,y in points],
        [tuple(reversed(range(n))),tuple(range(n,2*n))]+
        [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],mat)

def arc_points(center,r,a,b,steps=None):
    # A two-metre annular walk loses under 1mm of usable chord width at 3.5deg.
    steps=steps or math.ceil(abs(b-a)/3.5)
    sign=1 if b>a else -1
    angles=sorted(set([a,b,a+sign*.05,b-sign*.05]+[a+(b-a)*i/steps for i in range(1,steps)]),reverse=b<a)
    return [(center[0]+r*math.cos(math.radians(angle)),center[1]+r*math.sin(math.radians(angle))) for angle in angles]

def corner_path(center,r,a,b):
    sign=1 if b>a else -1
    return [sample(p,(-sign*(p[1]-center[1])/r,sign*(p[0]-center[0])/r)) for p in arc_points(center,r,a,b)]

JUNCTIONS=[]
def junction(name,main,branch,tee,D):
    h=main*1.5;b=branch*1.5;r=3
    if tee:
        ne=arc_points((b+r,h+r),r,-90,-180)
        nw=arc_points((-b-r,h+r),r,0,-90)
        points=[(-D,-h),(D,-h),(D,h)]+ne+[(b,D),(-b,D)]+nw+[(-D,h)]
        corners=[((b+r,h+r),-90,-180,[(D,h+1.12),(b+1.12,D)]),
                 ((-b-r,h+r),0,-90,[(-b-1.12,D),(-D,h+1.12)])]
        dirs=[(-1,0,main),(1,0,main),(0,1,branch)]
    else:
        points=[(-D,-h)]+arc_points((-h-r,-h-r),r,90,0)+[(-h,-D),(h,-D)]+arc_points((h+r,-h-r),r,180,90)+[(D,-h),(D,h)]+arc_points((h+r,h+r),r,-90,-180)+[(h,D),(-h,D)]+arc_points((-h-r,h+r),r,0,-90)+[(-D,h)]
        corners=[((-h-r,-h-r),90,0,[(-D,-h-1.12),(-h-1.12,-D)]),
                 ((h+r,-h-r),180,90,[(h+1.12,-D),(D,-h-1.12)]),
                 ((h+r,h+r),-90,-180,[(D,h+1.12),(h+1.12,D)]),
                 ((-h-r,h+r),0,-90,[(-h-1.12,D),(-D,h+1.12)])]
        dirs=[(-1,0,main),(1,0,main),(0,1,main),(0,-1,main)]
    ports=[port(f'road_{dx}_{dy}',[dx*D,dy*D,0],[dx,dy,0],lanes*3,lanes) for dx,dy,lanes in dirs]
    name=module(name,[('Surface',slab(points,0,.2,'Road_Asphalt'),'drive_surface')],ports,'junction',
                road_outline=[[x,y,0] for x,y in points],lanes=main,approach_lanes=[q[2] for q in dirs],
                total_approach_lanes=sum(q[2] for q in dirs),port_radius_m=D,corner_radius_m=r,
                surface_is_single_outline=True,arrows_baked=False)
    g=Geo()
    for dx,dy,lanes in dirs:
        # Right-hand incoming half only; the central turning area remains unpainted.
        center=Vector((dx*(D-.45),dy*(D-.45),.003))+Vector((-dy,dx,0))*(lanes*.75)
        g.box(center,(.12,lanes*1.5-.35,.0007) if dx else (lanes*1.5-.35,.12,.0007),'Marking_White')
    mn=module(name.replace('RoadPlan_','RoadPlan_JunctionMarking_'),[('StopBars',g,'none')],category='marking',paint_only=True,right_hand_traffic=True)
    MODULES[name]['companions'].append(dict(module=mn,position_m=[0,0,0],yaw_degrees=0))
    specs=[];walkports=[]
    for i,(center,a,bb,ends) in enumerate(corners):
        path=corner_path(center,r-1.12,a,bb)
        specs.append((f'CornerPaving_{i}',sweep(path,2,'Walk_Paving',.15,.30),'walk_surface'))
        curbpath=corner_path(center,r-.06,a,bb)
        specs.append((f'CornerCurb_{i}',sweep(curbpath,.12,'Road_Edge',.15,.30),'walk_edge'))
        for j,(tip,q) in enumerate(zip(ends,[path[0],path[-1]])):
            p=q['position_m'];delta=Vector((tip[0]-p[0],tip[1]-p[1],0));d=delta.normalized()
            segment=[sample((p[0],p[1]),d[:2]),sample(tip,d[:2])]
            specs.append((f'ApproachPaving_{i}_{j}',sweep(segment,2,'Walk_Paving',.15,.30),'walk_surface'))
            # The curb stays on the road-facing side of each usable two-metre strip.
            toward=Vector(center)-Vector(tip);cross=d.x*toward.y-d.y*toward.x
            off=-1.06 if cross>0 else 1.06
            specs.append((f'ApproachCurb_{i}_{j}',sweep(segment,.12,'Road_Edge',.15,.30,off),'walk_edge'))
            walkports.append(port(f'walk_{i}_{j}',[tip[0],tip[1],.15],d,2,kind='walk',role='crosswalk_approach_route'))
    if tee:
        path=[sample((-D,-h-1.12)),sample((D,-h-1.12))]
        specs.extend([('ClosedSidePaving',sweep(path,2,'Walk_Paving',.15,.30),'walk_surface'),
                      ('ClosedSideCurb',sweep(path,.12,'Road_Edge',.15,.30,1.06),'walk_edge')])
        walkports+=endpoints(path,2,kind='walk',walk=True)
    wn=module(name.replace('RoadPlan_','RoadPlan_WalkCorner_'),specs,walkports,'walk','P1',
              road_companion=name,effective_walk_width_m=2,curb_width_m=.12,
              minimum_piecewise_linear_walk_width_m=2*math.cos(math.radians(90/math.ceil(90/3.5)/2)),width_tolerance_m=.001,
              crossing_access='Continue these walk ports to an external four-metre crossing and separate accessible ramp; do not bake a crossing through the turn zone')
    MODULES[name]['companions'].append(dict(module=wn,position_m=[0,0,0],yaw_degrees=0))
    JUNCTIONS.append(name)

junction('T_4L_2L',4,2,True,10)
junction('T_2L',2,2,True,7)
junction('X_2L',2,2,False,7)

# Smooth symmetric taper, with endpoint guards for physical edge tangent agreement.
times=sorted(set([0,.002,.998,1]+[i/24 for i in range(1,24)]))
transition=[sample((24*(t-.5),0)) for t in times]
width_at=lambda t:12-6*(3*t*t-2*t*t*t)
# Sweep's index parameter is deliberately replaced by the actual longitudinal parameter.
def taper_sweep(width,mat,z,depth,offset=0):
    g=Geo();verts=[]
    for t,q in zip(times,transition):
        w=width(t) if callable(width) else width;off=offset(t) if callable(offset) else offset;x=q['position_m'][0]
        verts += [(x,off-w/2,z-depth),(x,off+w/2,z-depth),(x,off+w/2,z),(x,off-w/2,z)]
    faces=[(3,2,1,0)];n=len(times)-1
    for i in range(n):
        for k in range(4):faces.append((i*4+k,(i+1)*4+k,(i+1)*4+(k+1)%4,i*4+(k+1)%4))
    faces.append((n*4,n*4+1,n*4+2,n*4+3));return g.add(verts,faces,mat)

tn=module('Transition_4L_2L',[('Surface',taper_sweep(width_at,'Road_Asphalt',0,.2),'drive_surface')],
    [port('wide',[-12,0,0],[-1,0,0],12,4),port('narrow',[12,0,0],[1,0,0],6,2)],'road',
    start_lanes=4,end_lanes=2,length_m=24,width_profile='cubic_smoothstep',centerline_samples=transition,
    road_outline=[[q['position_m'][0],-width_at(t)/2,0] for t,q in zip(times,transition)]+
                 [[q['position_m'][0],width_at(t)/2,0] for t,q in reversed(list(zip(times,transition)))],
    integration_note='Replace temporary linear taper and its port spacing together; final ports are 24m apart')
specs=[('Center',taper_sweep(.085,'Marking_Center',.003,.0007),'none')]
for s in [-1,1]:
    specs.append((f'Edge_{s}',taper_sweep(.075,'Marking_White',.003,.0007,lambda t,s=s:s*(width_at(t)/2-.15)),'none'))
    g=Geo()
    for x in [-10,-6,-2,2,6,10]:
        t=(x+12)/24;y=s*3*(1-(3*t*t-2*t*t*t));g.box((x,y,.003),(1.4,.075,.0007),'Marking_White')
    specs.append((f'MergeDash_{s}',g,'none'))
mn=module('TransitionMarking_4L_2L',specs,category='marking',paint_only=True,arrows_baked=False)
MODULES[tn]['companions'].append(dict(module=mn,position_m=[0,0,0],yaw_degrees=0))
specs=[];ports=[]
for s in [-1,1]:
    specs.extend([(f'Paving_{s}',taper_sweep(2,'Walk_Paving',.15,.30,lambda t,s=s:s*(width_at(t)/2+1.12)),'walk_surface'),
                  (f'Curb_{s}',taper_sweep(.12,'Road_Edge',.15,.30,lambda t,s=s:s*(width_at(t)/2+.06)),'walk_edge')])
    ports.extend([port(f'{s}_wide',[-12,s*7.12,.15],[-1,0,0],2,kind='walk'),port(f'{s}_narrow',[12,s*4.12,.15],[1,0,0],2,kind='walk')])
wn=module('WalkTransition_4L_2L',specs,ports,'walk','P1',road_companion=tn,effective_walk_width_m=2,curb_width_m=.12)
MODULES[tn]['companions'].append(dict(module=wn,position_m=[0,0,0],yaw_degrees=0))

for lanes in [2,4]:
    width=lanes*3;g=Geo()
    count=round(width/.75)
    for i in range(count):g.box((0,-width/2+.375+i*.75,.003),(3.6,.36,.0007),'Marking_White')
    module(f'CrosswalkPaint_{lanes}L',[('Paint',g,'none')],category='marking',paint_only=True,
           nominal_footprint_m=[4,width],actual_stripe_length_m=3.6,lanes=lanes,
           reuse_review='Rebuilt Transit stripe rhythm at nominal 4m band, required shared material and 3mm offset; no pavement')
    g=Geo()
    for side in [-1,1]:
        for x in [-1.75,1.75]:
            g.box((x,side*(width/2+.06),.004),(.32,.025,.001),'Signal_Emission')
            g.box((x,side*(width/2+.16),.004),(.025,.22,.001),'Signal_Emission')
    module(f'PedestrianHolo_{lanes}L',[('GroundBrackets',g,'none')],category='signal',priority='P1',
           signal_binding='external pedestrian signal state / material parameter',collision_generated=False,no_poles=True)

for width in [2,4]:
    path=[]
    for t in sorted(set([.005,.995]+[i/16 for i in range(17)])):
        z=.15*(1-3*t*t+2*t*t*t);slope=-.15/4*6*t*(1-t)
        path.append(sample((-2+4*t,0),z=z,slope=slope))
    specs=[('Surface',sweep(path,width,'Walk_Paving',0,.20),'walk_surface')]
    name=module(f'AccessibleRamp_W{width}',specs,
        [port('raised',[-2,0,.15],[-1,0,0],width,kind='walk'),port('flush',[2,0,0],[1,0,0],width,kind='walk')],
        'walk','P1',effective_walk_width_m=width,length_m=4,height_delta_m=.15,max_grade=.05625,
        centerline_samples=path,profile='cubic_smoothstep_with_flat_endpoint_tangents')
    tactile=[]
    for panel in range(width//2):
        g=Geo();cy=-width/2+1+panel*2
        for x in [-1.70,-1.54]:
            t=(x+2)/4;zz=.15*(1-3*t*t+2*t*t*t)
            for j in range(6):g.rod((x,cy-.72+j*.288,zz+.001),(x,cy-.72+j*.288,zz+.004),.022,.022,'Marking_Center',sides=6)
        tactile.append((f'TactileCassette_{panel}',g,'none'))
    mn=module(f'AccessibleTactile_W{width}',tactile,category='marking',priority='P1',paint_only=False,tactile_projection_m=.004)
    MODULES[name]['companions'].append(dict(module=mn,position_m=[0,0,0],yaw_degrees=0))

def upright(x,y,z,slope=0):
    g=Geo();rot=Matrix.Rotation(-math.atan(slope),3,'Y')
    g.box((x,y,z+.035),(.24,.24,.07),'Barrier_Metal',rot=rot)
    g.box((x,y,z+.57),(.065,.075,1.05),'Barrier_Metal')
    for side in [-1,1]:g.box((x,y+side*.045,z+.77),(.075,.012,.095),'Signal_Emission')
    for dx in [-.075,.075]:g.box((x+dx,y-.075,z+.075),(.028,.028,.023),'Barrier_Metal')
    return g

def parapet(length):
    path=linear(length);specs=[]
    for z,w,d,label in [(1.10,.11,.07,'TopRail'),(.56,.075,.06,'MidRail'),(.12,.12,.11,'ToeRail')]:
        specs.append((label,sweep(path,w,'Barrier_Metal',z,d),'barrier'))
    for i,x in enumerate([-length/2+.35+j*(length-.7)/round(length/2) for j in range(round(length/2)+1)]):
        specs.append((f'Upright_{i}',upright(x,0,0),'barrier'))
    return module(f'Parapet_L{length}',specs,category='barrier',priority='P1',nominal_length_m=length,
                  road_surface_included=False,inner_edge_offset_from_centerline_m=3.04,required_lateral_placement_m=3.16)

for length in [4,8,16]:parapet(length)

def deck_structure(length):
    specs=[]
    for side in [-1,1]:
        g=Geo();y=side*2.35
        for z,ww,hh in [(-.215,.44,.07),(-.305,.11,.11),(-.37,.44,.06)]:g.box((0,y,z),(length,ww,hh),'Barrier_Metal')
        for x in [-length/2+.5+j*2 for j in range(round(length/2))]:g.box((x,y,-.30),(.06,.39,.14),'Barrier_Metal')
        specs.append((f'Girder_{side}',g,'structure'))
    for side in [-1,1]:specs.append((f'Crosshead_{side}',Geo().box((side*(length/2-.16),0,-.30),(.26,6.3,.20),'Structure_Concrete'),'structure'))
    return module(f'DeckStructure_L{length}',specs,category='structure',priority='P1',
        structural_bottom_z_m=-.40,overhead_clearance=dict(nominal_ground_z_m=0,required_under_clearance_m=8,
        required_deck_surface_world_z_m=8.4),road_surface_included=False)

for length in [4,8,16]:
    name=path_road(f'Deck_2L_L{length}',linear(length),2,'deck',length_m=length,
                   overhead_clearance=dict(local_structural_bottom_z_m=-.4,recommended_surface_elevation_m=8.4,clear_height_at_recommended_m=8),
                   levels_do_not_auto_connect=True,priority='P1' if length==4 else 'P0')
    structure=deck_structure(length)
    MODULES[name]['companions'] += [dict(module=structure,position_m=[0,0,0],yaw_degrees=0)]
    for side in [-1,1]:MODULES[name]['companions'].append(dict(module=f'RoadPlan_Parapet_L{length}',position_m=[0,side*3.16,0],yaw_degrees=0))

for height,label in [(7.6,'7p6'),(8.0,'8')]:
    specs=[]
    for side in [-1,1]:
        g=Geo().rings([(0,side*1.9,.18),(0,side*1.45,height-.40)],[.38,.30],'Structure_Concrete',axis=(0,-side*.45,height-.58))
        g.box((0,side*1.9,.02),(1.4,1.2,.36),'Structure_Concrete')
        for dx in [-.44,.44]:
            for dy in [-.36,.36]:g.box((dx,side*1.9+dy,.22),(.08,.08,.08),'Barrier_Metal')
        specs.append((f'LegFoundation_{side}',g,'pier'))
    specs.append(('CapBeam',Geo().box((0,0,height-.20),(1.15,6.3,.40),'Structure_Concrete'),'pier'))
    module(f'Pier_H{label}',specs,category='structure',priority='P1',cap_height_m=height,
           reserved_ground_footprint_m=[1.6,6.5],placement_rule='Separate support at a clear station; never auto-place at road crossing',
           recommended_road_surface_z_m=height+.4)

module('Abutment_W6',[('Foundation',Geo().box((0,0,-.25),(1.3,6.5,.5),'Structure_Concrete'),'pier'),
                     ('BackWall',Geo().box((-.42,0,.15),(.26,6.3,.30),'Structure_Concrete'),'structure')],
       category='structure',priority='P1',placement_rule='Place outside the live road mouth; reserve retaining-wall volume')
module('Barrier_EndProtection',[('CrashCushion',Geo().box((0,0,.42),(.6,.32,.84),'Road_Edge'),'barrier'),
                                ('Reflector',Geo().box((-.31,0,.52),(.015,.24,.32),'Marking_Center'),'none')],
       category='barrier',priority='P1',placement_rule='Outside usable carriageway, at parapet ends')

RAMPS=[]
for rise,label in [(8,'8'),(8.4,'8p4')]:
    L=110;a=16;m=rise/(L-a)
    def height(x):
        if x<a:return m*x*x/(2*a)
        if x>L-a:return rise-m*(L-x)**2/(2*a)
        return m*(x-a/2)
    def grade(x):return m*x/a if x<a else m*(L-x)/a if x>L-a else m
    full=[];specs=[];marks=[];rails=[];girders=[];segments=[]
    for zone,start,end,steps in [('LowerVerticalCurve',0,16,32),('ConstantGrade',16,94,1),('UpperVerticalCurve',94,110,32)]:
        path=[sample((start+(end-start)*i/steps,0),z=height(start+(end-start)*i/steps),slope=grade(start+(end-start)*i/steps)) for i in range(steps+1)]
        full+=path if not full else path[1:]
        specs.append((zone,sweep(path,6,'Road_Asphalt',0,.20),'drive_surface'))
        marks+=marking_specs(path,6,2,zone+'_')
        coarse=[sample((start+(end-start)*i/(8 if steps>1 else 1),0),z=height(start+(end-start)*i/(8 if steps>1 else 1)),slope=grade(start+(end-start)*i/(8 if steps>1 else 1))) for i in range((8 if steps>1 else 1)+1)]
        for side in [-1,1]:
            for zz,ww,dd,role in [(1.10,.11,.07,'TopRail'),(.56,.075,.06,'MidRail'),(.12,.12,.11,'ToeRail')]:
                rails.append((f'{zone}_{role}_{side}',sweep(coarse,ww,'Barrier_Metal',zz,dd,side*3.16),'barrier'))
            g=Geo()
            for zz,ww,dd in [(-.18,.44,.07),(-.25,.11,.09),(-.34,.44,.06)]:sweep(coarse,ww,'Barrier_Metal',zz,dd,side*2.35,g)
            girders.append((f'{zone}_Girder_{side}',g,'structure'))
        segments.append(dict(id=zone,start_m=[start,0,height(start)],end_m=[end,0,height(end)],
                             start_tangent=path[0]['tangent'],end_tangent=path[-1]['tangent'],length_horizontal_m=end-start))
    for station in list(range(0,111,4)) + [110]:
        for side in [-1,1]:rails.append((f'Upright_{station}m_{side}',upright(station,side*3.16,height(station),grade(station)),'barrier'))
    # Coarse structural chords define the actual conservative underside envelope.
    knots=sorted(set([0,16,94,110]+list(range(0,17,2))+list(range(94,111,2))))
    def underside(x):
        for low,high in zip(knots,knots[1:]):
            if low<=x<=high:return height(low)+(height(high)-height(low))*(x-low)/(high-low)-.4
        return height(x)-.4
    clearance=[dict(x_m=x,underside_z_m=underside(x),pedestrian_2p2m_allowed=underside(x)>=2.2,
                    vehicle_8m_allowed=underside(x)>=8-1e-6) for x in range(111)]
    name=module(f'Ramp_2L_Rise{label}_L110',specs,
        [port('lower',[0,0,0],[-1,0,0],6,2,0),port('upper',[110,0,rise],[1,0,0],6,2,1)],
        'ramp',rise_m=rise,horizontal_length_m=110,max_grade=m,grade_limit=.09,
        end_vertical_curve_length_m=a,centerline_samples=full,functional_segments=segments,
        road_outline=outline(full,6),clearance_envelope=clearance,
        overhead_clearance=dict(at_upper_port_m=rise-.4,nominal_ground_z_m=0,
          satisfies_8m_at_upper=rise>=8.4,low_under_ramp_space_reserved=True),
        integration_note='Rise8 matches current graph; Rise8p4 requires graph altitude 4.2 world units and matching deck placement. No code changed.')
    mn=module(f'RampMarking_2L_Rise{label}_L110',marks,category='marking',paint_only=True)
    rn=module(f'RampParapet_2L_Rise{label}_L110',rails,category='barrier',priority='P1',road_surface_included=False,inner_edge_from_centerline_m=3.04)
    sn=module(f'RampStructure_2L_Rise{label}_L110',girders,category='structure',priority='P1',local_structural_depth_m=.4)
    MODULES[name]['companions'] += [dict(module=n,position_m=[0,0,0],yaw_degrees=0) for n in [mn,rn,sn]]
    RAMPS.append(name)

for length in [2,4,8]:
    module(f'Curb_L{length}',[('ChamferedCurb',Geo().box((0,0,0),(length,.12,.30),'Road_Edge'),'walk_edge')],
           category='edge',priority='P1',nominal_top_z_m=.15,width_m=.12,road_surface_included=False)
g=Geo()
g.box((0,0,-.038),(2,.10,.075),'Road_Edge')
for x in [-.94,.94]:g.box((x,0,-.012),(.07,.10,.024),'Barrier_Metal')
bars=Geo()
for i in range(16):bars.box((-.86+i*.115,0,-.007),(.055,.085,.014),'Barrier_Metal')
module('Drain_L2',[('RecessedChannel',g,'edge_drain'),('ReplaceableGrate',bars,'edge_drain')],category='edge',priority='P1',
       top_z_m=0,width_m=.10,road_surface_included=False,placement_rule='Within the separate .12m edge reservation; grate flush at Z0')
for lanes in [2,4]:module(f'EdgeCap_{lanes}L',[('EndClosure',Geo().box((0,0,-.10),(.06,lanes*3,.20),'Road_Edge'),'surface_edge')],
                       category='edge',priority='P1',lanes=lanes,nominal_top_z_m=0,road_surface_included=False)

for lanes in [2,4]:
    for length in [4,8,16]:
        info=MODULES[f'RoadPlan_Surface_{lanes}L_L{length}']
        pieces=[(0,length)] if length<=8 else [(-4,8),(4,8)]
        for side in [-1,1]:
            for x,piece_length in pieces:
                info['companions'].extend([
                    dict(module=f'RoadPlan_Walk_W2_L{piece_length}',position_m=[x,side*(lanes*1.5+1.12),0],yaw_degrees=0),
                    dict(module=f'RoadPlan_Curb_L{piece_length}',position_m=[x,side*(lanes*1.5+.06),0],yaw_degrees=0)])

# UVs use shared metre-based lateral coordinates and integer longitudinal phase.
# Analytic ramp normals preserve endpoint continuity while respecting real grade.
def finish_surface(obj,module_info):
    path=module_info.get('centerline_samples')
    if not path:return
    length=sum(math.dist(a['position_m'][:2],b['position_m'][:2]) for a,b in zip(path,path[1:]))
    periods=max(1,round(length/2));mesh=obj.data;uv=mesh.uv_layers.active
    positions=[Vector(p['position_m']) for p in path]
    lengths=[0]
    for a,b in zip(positions,positions[1:]):lengths.append(lengths[-1]+(Vector(b[:2])-Vector(a[:2])).length)
    projected=[]
    for v in mesh.vertices:
        best=None
        for i,(a,b) in enumerate(zip(positions,positions[1:])):
            axis=Vector((b.x-a.x,b.y-a.y,0));q=Vector((v.co.x-a.x,v.co.y-a.y,0));t=max(0,min(1,q.dot(axis)/axis.length_squared))
            delta=q-axis*t;dist=delta.length_squared
            if best is None or dist<best[0]:
                along=lengths[i]+axis.length*t;lat=axis.normalized().cross(delta).z
                slope=path[i]['slope']*(1-t)+path[i+1]['slope']*t
                direction=axis.normalized();best=(dist,along,lat,Vector((-slope*direction.x,-slope*direction.y,1)).normalized())
        for index,along in [(0,0),(-1,length)]:
            q=path[index];p=positions[index];dx,dy=q['direction_xy']
            if abs((v.co.x-p.x)*dx+(v.co.y-p.y)*dy)<1e-5:
                lat=-(v.co.x-p.x)*dy+(v.co.y-p.y)*dx
                best=(0,along,lat,Vector((-q['slope']*dx,-q['slope']*dy,1)).normalized())
        projected.append(best)
    normals=[None]*len(mesh.loops)
    for p in mesh.polygons:
        for li in p.loop_indices:
            index=mesh.loops[li].vertex_index;q=projected[index]
            if abs(p.normal.z)>.65:
                uv.data[li].uv=(q[1]/length*periods,q[2]/2)
            else:
                dominant=max(range(3),key=lambda k:abs(p.normal[k]));axes=[k for k in range(3) if k!=dominant]
                co=mesh.vertices[index].co;uv.data[li].uv=(co[axes[0]]*.5,co[axes[1]]*.5)
            normals[li]=tuple(q[3] if p.normal.z>.65 else p.normal)
    mesh.normals_split_custom_set(normals)
    mesh.update()

records={r['id']:r for r in PIPE.records}
for a in ASSETS:
    # MikkTSpace requires triangles/quads; retained planar ngons can otherwise
    # produce missing or zero tangents at collinear boundary vertices.
    ob=PROTOTYPES[a['id']];bm=bmesh.new();bm.from_mesh(ob.data)
    bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
    bm.to_mesh(ob.data);bm.free();ob.data.update();ob.data.calc_loop_triangles()
    count=len(ob.data.loop_triangles)
    assert count==a['triangles'],(a['id'],'tessellation changed triangle budget')
    a['polygons']=len(ob.data.polygons)
    records[a['id']]['operations'].append('explicit_export_tessellation_for_valid_mikktspace_tangents')
    if a['collision_role'] not in ['drive_surface','walk_surface']:continue
    info=MODULES[a['module_id']]
    finish_surface(PROTOTYPES[a['id']],info)
    finish_surface(bpy.data.objects[a['detail_source']],info)

summary=dict(modules=len(MODULES),parts=len(ASSETS),source_triangles=sum(r['source_triangles'] for r in PIPE.records),
    optimized_triangles=sum(r['optimized_triangles'] for r in PIPE.records),removed_triangles=sum(r['removed_triangles'] for r in PIPE.records),
    max_triangles=max(a['triangles'] for a in ASSETS),over_budget=[(a['id'],a['triangles']) for a in ASSETS if a['triangles']>500])
(ROOT/'Validation/road_plan_draft.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print('ROAD_PLAN_GEOMETRY',json.dumps(summary),flush=True)
assert not summary['over_budget'],summary['over_budget']
if '--geometry-check' in sys.argv:raise SystemExit(0)

def write(path,value):
    target=ROOT/path;temporary=target.with_suffix(target.suffix+'.new')
    temporary.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');temporary.replace(target)

def export(file,names,kind):
    bpy.context.window.scene=scene
    for o in scene.objects:o.select_set(False)
    for name in names:PROTOTYPES[name].select_set(True)
    p=ROOT/file;p.parent.mkdir(parents=True,exist_ok=True);tmp=p.with_name(p.stem+'.staging.glb')
    bpy.ops.export_scene.gltf(filepath=str(tmp),export_format='GLB',use_selection=True,use_active_scene=True,
        export_animations=False,export_skins=False,export_yup=True,export_extras=True,export_cameras=False,export_lights=False,
        export_materials='EXPORT',export_apply=False,export_texcoords=True,export_normals=True,export_tangents=True)
    repairs=repair_tangents(tmp)
    if repairs:TANGENT_REPAIRS[file]=repairs
    tmp.replace(p);EXPORTS.append(dict(file=file,type=kind,mesh_parts=names,clips=0,expansion=TAG))

for name,info in MODULES.items():
    export(f'Export/RoadPlan/{name}.glb',info['parts'],'scenery')
    for pid in info['parts']:export(f'Export/RoadPlan/Parts/{pid}.glb',[pid],'module')

# Presentation and integration recipes: positive unit scales; no runtime edits.
tree=ast.parse((ROOT/'Tools/build_air_city_expansion.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in {'tagged','studio','label','fit_camera'}],type_ignores=[]),'<studio helpers>','exec'))
PREVIEWS=[]
def new_scene(name,center,span,res):
    s,c=studio(name,center,span,res);s.view_settings.exposure=.55
    EXAMPLES[name]=dict(placements=[],connections=[],checks=[])
    PREVIEWS.append((s,'',c));return s,c

def place(c,name,xyz=(0,0,0),yaw=0,companions=True,pid=None):
    if not name.startswith('RoadPlan_'):name='RoadPlan_'+name
    model=MODULES[name];rot=Matrix.Rotation(yaw,3,'Z')
    example=next((EXAMPLES[s.name] for s in bpy.data.scenes if c.name in s.collection.children),None)
    index=None
    if example is not None:
        index=len(example['placements']);example['placements'].append(dict(id=pid or f'{name}_{index}',module=name,position_m=list(xyz),yaw_degrees=math.degrees(yaw),scale=[1,1,1]))
    for n in model['parts']:
        ob=tagged(PROTOTYPES[n].copy());ob.asset_clear();ob.parent=None;c.objects.link(ob)
        ob.location=xyz;ob.rotation_euler[2]=yaw;ob.modifiers.clear()
    if companions:
        for comp in model['companions']:
            place(c,comp['module'],Vector(xyz)+rot@Vector(comp['position_m']),yaw+math.radians(comp['yaw_degrees']),False)
    return index

def connect(s,a,pa,b,pb):EXAMPLES[s.name]['connections'].append(dict(a=a,port_a=pa,b=b,port_b=pb))

s,c=new_scene('29_RoadPlan_Ground_Connections',(20,15,0),110,(2600,1800))
PREVIEWS[-1]=(s,'32_road_plan_ground',c)
for lanes,y in [(2,-13),(4,8)]:
    cursor=-30;last=None
    for length in [4,8,16]:
        index=place(c,f'Surface_{lanes}L_L{length}',(cursor+length/2,y,0));
        if last is not None:connect(s,last,'end',index,'start')
        last=index;cursor+=length
    label(c,f'{lanes} LANES / 3m EACH / L4 + L8 + L16',(-15,y-8,-.18),.45)
for name,xyz in [('T_4L_2L',(24,-10,0)),('T_2L',(23,20,0)),('X_2L',(48,20,0)),('Transition_4L_2L',(-15,32,0))]:place(c,name,xyz)
place(c,'Curve_2L_A90_R7',(53,-20,0));place(c,'Curve_2L_A90_R7',(82,-13,0),math.pi)
label(c,'T 4+4+2 / PORT 10m',(24,-23,-.18),.45)
label(c,'SMALL T / X / PORT 7m',(36,34,-.18),.45)
label(c,'R7 / LEFT AND REVERSED RIGHT',(68,-27,-.18),.42)

s,c=new_scene('30_RoadPlan_Curves_Transition',(35,30,0),145,(2600,1800));PREVIEWS[-1]=(s,'33_road_plan_curves',c)
for i,name in enumerate(CURVES):
    xy=((i%3)*40,(i//3)*38,0);place(c,name,xy)
    label(c,name.replace('RoadPlan_',''),(xy[0]+8,xy[1]-8,-.18),.44)
place(c,'Transition_4L_2L',(55,84,0));label(c,'WALK WIDTH 2m / CURB 0.12m / L24 TAPER',(55,96,-.18),.50)

s,c=new_scene('31_RoadPlan_Accessibility',(10,0,0),55,(2400,1600));PREVIEWS[-1]=(s,'34_road_plan_accessibility',c)
for lanes,cx,width in [(2,0,2),(4,22,4)]:
    h=lanes*1.5
    place(c,f'Surface_{lanes}L_L4',(cx,0,0),companions=False)
    place(c,f'CrosswalkPaint_{lanes}L',(cx,0,0));place(c,f'PedestrianHolo_{lanes}L',(cx,0,0))
    for side in [-1,1]:
        ramp=place(c,f'AccessibleRamp_W{width}',(cx,side*(h+2),0),-side*math.pi/2)
        walkname='Walk_W2_L4' if width==2 else 'WalkLanding_W4_L2';length=4 if width==2 else 2
        landing=place(c,walkname,(cx,side*(h+4+length/2),0),side*math.pi/2)
        connect(s,ramp,'raised',landing,'start')
    label(c,f'{lanes}L PAINT ONLY / W{width} ACCESS RAMP',(cx,-h-10,-.18),.35)

def add_span(c,center,height=8.4,length=16):return place(c,f'Deck_2L_L{length}',(center,0,height))
s,c=new_scene('32_RoadPlan_Overpass_Assembly',(200,0,4),440,(3000,1200));PREVIEWS[-1]=(s,'35_road_plan_overpass',c)
for v in next(o for o in c.objects if o.name.endswith('_Ground')).data.vertices:v.co.x*=4;v.co.y*=4
first=place(c,'Ramp_2L_Rise8p4_L110');cursor=110;last=first;lastport='upper'
for length in [16]*11+[4]:
    current=add_span(c,cursor+length/2,length=length)
    connect(s,last,lastport,current,'start');last=current;lastport='end';cursor+=length
down=place(c,'Ramp_2L_Rise8p4_L110',(cursor+110,0,0),math.pi);connect(s,current,'end',down,'upper')
bridge_piers=[118,142,154,178,202,226,250,282]
for x in bridge_piers:place(c,'Pier_H8',(x,0,0),companions=False)
EXAMPLES[s.name]['checks'].append(dict(flat_reach_m=180,nominal_current_flat_reach_m=180,
    closure_module='RoadPlan_Deck_2L_L4',ground_to_ground_horizontal_m=400,
    ground_crossing_station_m=166,ground_walk_outer_edge_m=8.12,pier_stations_m=bridge_piers))
EXAMPLES[s.name]['game_layout_binding']=dict(origin_m=[96,-200,0],yaw_degrees=90,game_scale=.5,
    lower_world_xy=[48,-100],upper_world_xy=[48,-45],opposite_upper_world_xy=[48,45],opposite_lower_world_xy=[48,100],
    current_upper_altitude_world=4,recommended_upper_altitude_world=4.2)
s.camera.location=(210,-410,180);s.camera.rotation_euler=(Vector((200,0,3))-s.camera.location).to_track_quat('-Z','Y').to_euler()
label(c,'L110 RAMP / MAX 8.94% / DECK Z8.4m / CLEAR UNDER 8m',(200,-25,-.18),3.5)

s,c=new_scene('33_RoadPlan_GradeSeparated_Crossing',(0,0,3),75,(2400,1700));PREVIEWS[-1]=(s,'36_road_plan_grade_separation',c)
left=add_span(c,-8);right=add_span(c,8);connect(s,left,'end',right,'start')
for x in [-12,12]:place(c,'Pier_H8',(x,0,0),companions=False)
for y in [-16,0,16]:
    place(c,'Surface_4L_L16',(0,y,0),math.pi/2)
label(c,'LEVEL 1 / DECK SURFACE 8.4m',(0,-8,10.2),.48)
label(c,'LEVEL 0 / INDEPENDENT PORTS / 8m UNDERCLEARANCE',(0,-28,-.18),.56)
EXAMPLES[s.name]['checks'].append(dict(kind='grade_separated_crossing',deck_surface_z_m=8.4,lowest_structure_z_m=8.0,
    ground_surface_z_m=0,clearance_m=8,pier_stations_m=[-12,12],ground_road_half_width_m=6,
    ground_walk_outer_edge_m=8.12,no_graph_connection_between_levels=True))

s,c=new_scene('34_RoadPlan_Structure_Details',(0,0,3),35,(2400,1700));PREVIEWS[-1]=(s,'37_road_plan_structure_details',c)
place(c,'Deck_2L_L8',(-5,0,9.3),companions=False)
place(c,'DeckStructure_L8',(-5,0,8.5),companions=False)
for side in [-1,1]:place(c,'Parapet_L8',(-5,side*3.16,9.3),companions=False)
place(c,'Pier_H8',(-5,0,0),companions=False)
place(c,'Drain_L2',(6,-2,0),companions=False);place(c,'Curb_L4',(6,-.5,0),companions=False)
place(c,'Barrier_EndProtection',(8,4,0),companions=False)
place(c,'Walk_W2_EndCap_L1',(6,-5,0),companions=False)
label(c,'SEPARATE SLAB / GIRDERS / VERTICAL POSTS / FOOT ANCHORS',(-3,-9,-.18),.38)

# Side-elevation diagram has true horizontal/vertical scale; dimensions are display-only.
s,c=new_scene('35_RoadPlan_Side_Elevation',(55,0,4),130,(2600,1100));PREVIEWS[-1]=(s,'38_road_plan_ramp_elevation',c)
place(c,'Ramp_2L_Rise8p4_L110',companions=True)
add_span(c,118)
s.camera.location=(55,-200,12);s.camera.rotation_euler=(Vector((55,0,4))-s.camera.location).to_track_quat('-Z','Y').to_euler()
def side_caption(text,pos,size):
    ob=label(c,text,pos,size);ob.rotation_euler[0]=math.pi/2;return ob
side_caption('110m HORIZONTAL / 8.4m RISE / 16m END VERTICAL CURVES',(55,-6,-3.0),1.2)
side_caption('MAX GRADE 8.936%  <  9%',(55,-6,12.8),1.2)
side_caption('SURFACE 8.4m / STRUCTURE BOTTOM 8.0m',(102,-6,18),1.0)
style_elevation()
for ss,file,col in PREVIEWS:fit_camera(ss)

assert protected_before==protected_hash(),'existing library geometry changed'
write('Documentation/road_plan_modules.json',dict(schema=REVISION,units='meters',authoring_up='+Z',gltf_up='+Y',
    coordinate_mapping='(x,y,z) -> (x,z,-y)',game_scale=.5,lane_width_m=3,curb_width_m=.12,effective_walk_width_m=2,
    source_request='Resource/ROAD_MODULE_REQUEST.md',materials=['Road_Asphalt','Road_Edge','Walk_Paving','Marking_White','Marking_Center','Barrier_Metal','Structure_Concrete','Signal_Emission'],
    placement_offset_policy='Source road Z0; apply the same game placement offset to ground and elevated roads, do not bake it twice',
    integration_status='art_delivery_only; game graph, collision and Runtime untouched',modules=MODULES))
write('Documentation/road_plan_examples.json',dict(units='meters',examples=EXAMPLES))
write('Validation/road_plan_pipeline_report.json',dict(schema='detail_first_lossless_cleanup_v1',summary=summary,
    source_scene=rawscene.name,parts=PIPE.records,protected_previous_geometry_sha256=protected_before,existing_assets_preserved=True,
    tangent_repaired_vertices_by_export=TANGENT_REPAIRS,tangent_policy='Retain valid MikkTSpace data; repair invalid zero tangents from actual UV derivatives without changing geometry'))
catalog['assets']=[a for a in catalog['assets'] if a.get('expansion')!=TAG]+ASSETS
catalog['exports']=[e for e in catalog['exports'] if e.get('expansion')!=TAG]+EXPORTS
write('Documentation/asset_catalog.json',catalog)
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
recipes={k:v for k,v in recipes.items() if not isinstance(v,dict) or v.get('expansion')!=TAG}
recipes.update({name:dict(parts=info['parts'],expansion=TAG,companions=info['companions']) for name,info in MODULES.items()})
write('Documentation/assembly_recipes.json',recipes)
overall=json.loads((ROOT/'Validation/source_report.json').read_text(encoding='utf-8'))
overall.update(mesh_parts=len(catalog['assets']),max_triangles=max(a['triangles'] for a in catalog['assets']),all_parts_under_500=True)
write('Validation/source_report.json',overall)
bpy.context.window.scene=bpy.data.scenes[entry_scene]
bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE),compress=True)
# Write a normal editable project with the gallery active, rather than an
# append-only data-block library that opens to an empty scene.
write_project(ROOT/'Source/RoadPlan_Modules.blend')
print('ROAD_PLAN_SAVED',len(MODULES),'MODULES',len(ASSETS),'PARTS',len(EXPORTS),'EXPORTS',flush=True)
if '--no-render' not in sys.argv:
    for ss,file,col in PREVIEWS:
        bpy.context.window.scene=ss;ss.render.filepath=str(ROOT/'Preview'/f'{file}.png')
        bpy.ops.render.render(write_still=True);print('ROAD_PLAN_PREVIEW',file,flush=True)
print('ROAD_PLAN_COMPLETE',flush=True)
