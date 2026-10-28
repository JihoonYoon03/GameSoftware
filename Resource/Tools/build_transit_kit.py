"""Blender authoring only: modular roads, pavements and unobtrusive air guides."""
import ast
import json
import math
import sys
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector, Matrix, Quaternion

ROOT = Path(__file__).resolve().parents[1]
assert ROOT.name == 'Resource'
SOURCE = ROOT/'Source/Erma_Resource_Library.blend'
TAG = 'transit_kit_v1'
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
bpy.context.preferences.filepaths.save_version = 0
for s in list(bpy.data.scenes):
    if s.get('expansion') == TAG: bpy.data.scenes.remove(s)
for o in list(bpy.data.objects):
    if o.get('expansion') == TAG: bpy.data.objects.remove(o, do_unlink=True)
for c in list(bpy.data.collections):
    if c.get('expansion') == TAG: bpy.data.collections.remove(c)
scene = bpy.data.scenes['00_Library']
bpy.context.window.scene = scene
MATS = {m.name:m for m in bpy.data.materials}
ASSETS, PROTOTYPES, EXPORTS, MODULES = [], {}, [], {}
tree = ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
node = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'Geo')
exec(compile(ast.Module(body=[node],type_ignores=[]),'<existing Geo>','exec'))
# Share the established lighting, layout and authoring helpers without running its builder.
tree = ast.parse((ROOT/'Tools/build_air_city_expansion.py').read_text(encoding='utf-8'))
names = {'tagged','studio','label','display','fit_camera'}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[]),'<studio helpers>','exec'))

for name,color,glow in [('Walk_Paver',(.32,.41,.41),0),('Walk_Tactile',(.58,.41,.17),0),
                        ('AirGuide_Subtle',(.12,.42,.45),.25)]:
    m=bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color=(*color,1)
    m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Roughness'].default_value=.85
    p.inputs['Emission Color'].default_value=(*color,1)
    p.inputs['Emission Strength'].default_value=glow
    MATS[name]=m

lib=bpy.data.collections.new('14_Transit_Kit')
lib['expansion']=TAG
bpy.data.collections['RESOURCE_LIBRARY'].children.link(lib)

def port(name,p,d,w,z=0,kind='road'):
    return dict(id=name,position_m=[p[0],p[1],z],outward=[d[0],d[1],0],width_m=w,kind=kind)

def module(name,specs,ports,category,**meta):
    c=bpy.data.collections.new(name+'_Parts');c['expansion']=TAG;lib.children.link(c)
    parts=[]
    for slot,g in specs:
        if not g.f: continue
        o=g.make(name+'_'+slot,c,None,slot,'transit.'+category)
        o['expansion']=TAG
        o['collision_role']='none' if category=='airguide' or 'Mark' in slot or 'Tactile' in slot else 'surface'
        o['authoring_up']='+Z'
        ASSETS[-1]['expansion']=TAG
        parts.append(o.name)
    MODULES[name]=dict(parts=parts,category=category,ports=ports,**meta)
    return name

def line_path(length):
    return lambda t: ((length*(t-.5),0),(1,0))

def curve_path(radius,angle):
    # Start at (0,0), incoming travel +X, left turn. Unlike legacy arcs, no remote origin.
    a=math.radians(angle)
    return lambda t: ((radius*math.sin(a*t),radius*(1-math.cos(a*t))),
                      (math.cos(a*t),math.sin(a*t)))

def ribbon(g,path,start,end,width,z,depth,mat,steps=1,offset=0,z_end=None):
    """Closed ribbon with analytic endpoint cross-sections and shared internal rings."""
    verts=[]
    for i in range(steps+1):
        q=i/steps;t=start+(end-start)*q
        (x,y),(dx,dy)=path(t)
        zz=z if z_end is None else z+(z_end-z)*q
        for off,h in [(offset-width/2,zz-depth),(offset+width/2,zz-depth),
                      (offset+width/2,zz),(offset-width/2,zz)]:
            verts.append((x-dy*off,y+dx*off,h))
    faces=[(3,2,1,0),(steps*4,steps*4+1,steps*4+2,steps*4+3)]
    for i in range(steps):
        for k in range(4): faces.append((i*4+k,(i+1)*4+k,(i+1)*4+(k+1)%4,i*4+(k+1)%4))
    return g.add(verts,faces,mat)

def path_ports(path,width,z=0,kind='road'):
    p0,d0=path(0);p1,d1=path(1)
    return [port('start',p0,(-d0[0],-d0[1]),width,z,kind),port('end',p1,d1,width,z,kind)]

def path_road(name,path,length,lanes,steps=1,**meta):
    w=lanes*3
    specs=[('Surface',ribbon(Geo(),path,0,1,w,0,.2,'Road_Basalt',steps))]
    # Each line is its own part; dashed marks remain below the polygon cap on long curves.
    for j in range(1,lanes):
        off=-w/2+j*3;g=Geo()
        if j==lanes//2:
            ribbon(g,path,0,1,.075,.012,.009,'Ceramic',steps,off)
        else:
            count=max(1,int(length/4))
            for k in range(count):
                t=(k+.5)/count;half=min(.7/length,.4/count)
                ribbon(g,path,t-half,t+half,.07,.012,.009,'Ceramic',max(1,math.ceil(steps*half*2)),off)
        specs.append((f'MarkLane{j}',g))
    for s in [-1,1]:
        specs.append((f'MarkEdge{ s:+d}',ribbon(Geo(),path,0,1,.07,.012,.009,'Ceramic',steps,s*(w/2-.13))))
    return module(name,specs,path_ports(path,w),'road',lanes=lanes,path_length_m=length,**meta)

def walk_strip(name,path,length,width=2,steps=1,offset=0,**meta):
    specs=[('Pavement',ribbon(Geo(),path,0,1,width,.15,.35,'Walk_Paver',steps,offset)),
           ('Curb',ribbon(Geo(),path,0,1,.12,.15,.35,'Ceramic',steps,offset-width/2+.06))]
    # Flush transverse expansion joints, no raised trip hazards.
    g=Geo()
    count=max(1,int(length/2))
    for i in range(count):
        t=(i+.5)/count
        ribbon(g,path,max(0,t-.012/length),min(1,t+.012/length),width-.2,.152,.003,'Steel',1,offset)
    specs.append(('MarkPaverJoints',g))
    pp=path_ports(path,width,.15,'walk')
    for p in pp:
        dx,dy=p['outward'][:2]
        # Both port offsets use travel normals, not outward normals.
        sign=-1 if p['id']=='start' else 1
        p['position_m'][0]-=dy*sign*offset;p['position_m'][1]+=dx*sign*offset
    return module(name,specs,pp,'walk',walk_width_m=width,path_length_m=length,**meta)

# 9 straight road sections, with exact legacy width and surface height.
for lanes in [2,4,6]:
    for length in [4,8,16]:
        path_road(f'Transit_Road_{lanes}L_Straight_{length}m',line_path(length),length,lanes)

# 24 tangent curves and their independently switchable left/right pedestrian companions.
CURVES=[]
for lanes in [2,4,6]:
    for radius in [18,36]:
        for angle in [15,30,45,90]:
            path=curve_path(radius,angle);length=radius*math.radians(angle)
            steps=math.ceil(angle/3.75)
            name=path_road(f'Transit_Road_{lanes}L_Curve{angle}_R{radius}',path,length,lanes,steps,
                           radius_m=radius,turn_degrees=angle)
            CURVES.append(name)
            specs=[];pp=[]
            for side,s in [('Left',1),('Right',-1)]:
                off=s*(lanes*1.5+1)
                specs.append((side+'Pavement',ribbon(Geo(),path,0,1,2,.15,.35,'Walk_Paver',steps,off)))
                specs.append((side+'Curb',ribbon(Geo(),path,0,1,.12,.15,.35,'Ceramic',steps,s*(lanes*1.5+.06))))
                for p in path_ports(path,2,.15,'walk'):
                    t=0 if p['id']=='start' else 1
                    (x,y),(dx,dy)=path(t)
                    p['position_m']=[x-dy*off,y+dx*off,.15];p['id']=side+'_'+p['id'];pp.append(p)
            walkname=module(name.replace('_Road_','_WalkSides_'),specs,pp,'walk',companion_road=name,
                           walk_width_m=2,turn_degrees=angle,radius_m=radius)
            MODULES[name]['walk_companion']=walkname

def slab_polygon(points,z,depth,mat):
    n=len(points)
    return Geo().add([(x,y,zz) for zz in [z-depth,z] for x,y in points],
        [tuple(reversed(range(n))),tuple(range(n,n*2))]+
        [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],mat)

def junction(lanes,tee):
    w=lanes*3;h=w/2;r=4;L=h+r
    def corner(sx,sy):
        # Clockwise outline traverses NE, SE, SW, NW concave corner arcs.
        a0={(1,1):180,(1,-1):90,(-1,-1):0,(-1,1):-90}[(sx,sy)]
        return [(sx*L+r*math.cos(math.radians(a0+i*90/12)),
                 sy*L+r*math.sin(math.radians(a0+i*90/12))) for i in range(13)]
    pts=[]
    if tee:
        pts=[(-L,h),(L,h)]+corner(1,-1)+corner(-1,-1)
    else:
        for sx,sy in [(1,1),(1,-1),(-1,-1),(-1,1)]: pts+=corner(sx,sy)
    # Points between arcs are the flush port edges, not separate overlapping rectangles.
    specs=[('Surface',slab_polygon(pts,0,.2,'Road_Basalt'))]
    dirs=[(-1,0),(1,0),(0,-1)]+([] if tee else [(0,1)])
    ports=[port(f'road_{dx}_{dy}',(dx*L,dy*L),(dx,dy),w) for dx,dy in dirs]
    # Minimal stop bars near each mouth; central turning area stays clear.
    for idx,(dx,dy) in enumerate(dirs):
        g=Geo()
        for j in range(lanes):
            off=-h+(j+.5)*3
            g.box((dx*(L-.45)-dy*off,dy*(L-.45)+dx*off,.012),
                  (.12,2.1,.009) if dx else (2.1,.12,.009),'Ceramic')
        specs.append((f'MarkStop{idx}',g))
    name=module(f'Transit_Road_{lanes}L_'+('T' if tee else 'X')+'Junction_R4',specs,ports,'road',
                lanes=lanes,corner_radius_m=r,footprint_m=[L*2,L*2])
    specs=[];pp=[]
    for sx,sy in ([(1,-1),(-1,-1)] if tee else [(1,1),(1,-1),(-1,-1),(-1,1)]):
        a0={(1,1):180,(1,-1):90,(-1,-1):0,(-1,1):-90}[(sx,sy)]
        def path(t,sx=sx,sy=sy,a0=a0):
            a=math.radians(a0+90*t)
            return ((sx*L+3*math.cos(a),sy*L+3*math.sin(a)),(-math.sin(a),math.cos(a)))
        specs += [(f'Corner{sx}{sy}',ribbon(Geo(),path,0,1,2,.15,.35,'Walk_Paver',12)),
                  (f'Curb{sx}{sy}',ribbon(Geo(),path,0,1,.12,.15,.35,'Ceramic',12,-.94))]
        for p in path_ports(path,2,.15,'walk'):p['id']=f'{sx}_{sy}_'+p['id'];pp.append(p)
    if tee:
        path=lambda t: ((-L+2*L*t,h+1),(1,0))
        specs += [('ClosedSideWalk',ribbon(Geo(),path,0,1,2,.15,.35,'Walk_Paver')),
                  ('ClosedSideCurb',ribbon(Geo(),path,0,1,.12,.15,.35,'Ceramic',offset=-.94))]
        for p in path_ports(path,2,.15,'walk'):p['id']='closed_'+p['id'];pp.append(p)
    wn=module(name.replace('_Road_','_WalkCorners_'),specs,pp,'walk',companion_road=name,walk_width_m=2)
    MODULES[name]['walk_companion']=wn
    return name

JUNCTIONS=[junction(lanes,t) for lanes in [2,4,6] for t in [False,True]]

# C1-smooth width transitions: endpoint shoulders have zero lateral derivative.
for la,lb,L in [(2,4,24),(4,6,24),(2,6,36)]:
    wa,wb=la*3,lb*3
    def wid(t):return wa+(wb-wa)*(3*t*t-2*t*t*t)
    def taper(g,edgeoff,width,z,depth,mat):
        # Cross-section center follows the shoulder, while all transverse edges remain flush.
        path=lambda t: ((L*(t-.5),edgeoff*wid(t)),(1,0))
        return ribbon(g,path,0,1,width,z,depth,mat,16)
    # Variable-width central slab.
    v=[]
    for i in range(17):
        t=i/16
        for y,z in [(-wid(t)/2,-.2),(wid(t)/2,-.2),(wid(t)/2,0),(-wid(t)/2,0)]:v.append((L*(t-.5),y,z))
    f=[(3,2,1,0),(64,65,66,67)]
    for i in range(16):
        for k in range(4):f.append((4*i+k,4*(i+1)+k,4*(i+1)+(k+1)%4,4*i+(k+1)%4))
    specs=[('Surface',Geo().add(v,f,'Road_Basalt')),
           ('MarkCenter',ribbon(Geo(),line_path(L),0,1,.075,.012,.009,'Ceramic'))]
    for s in [-1,1]:specs.append((f'MarkEdge{s}',taper(Geo(),s*.5,.07,.012,.009,'Ceramic')))
    # Lane boundaries emerge from the center, end exactly at the wider road's 3m lattice.
    for side in [-1,1]:
        for j in range(1,lb//2):
            g=Geo()
            for k in range(L//4):
                t=(k+.5)/(L//4);a=max(0,t-.7/L);b=min(1,t+.7/L)
                start_j=min(j,la//2-1)
                path=lambda t,side=side,j=j,start_j=start_j:((L*(t-.5),side*3*(start_j+(j-start_j)*(3*t*t-2*t*t*t))),(1,0))
                ribbon(g,path,a,b,.07,.012,.009,'Ceramic')
            specs.append((f'MarkMerge{side}_{j}',g))
    name=module(f'Transit_Road_Taper_{la}L_{lb}L_{L}m',specs,
                [port('start',(-L/2,0),(-1,0),wa),port('end',(L/2,0),(1,0),wb)],'road',
                start_lanes=la,end_lanes=lb,path_length_m=L,profile='cubic_smoothstep',reverse_allowed=True)
    specs=[];pp=[]
    for s in [-1,1]:
        path=lambda t,s=s:((L*(t-.5),s*(wid(t)/2+1)),(1,0))
        specs.append((f'Walk{s}',ribbon(Geo(),path,0,1,2,.15,.35,'Walk_Paver',16)))
        specs.append((f'Curb{s}',ribbon(Geo(),path,0,1,.12,.15,.35,'Ceramic',16,-s*.94)))
        for p in path_ports(path,2,.15,'walk'):p['id']=str(s)+'_'+p['id'];pp.append(p)
    wn=module(name.replace('_Road_','_WalkSides_'),specs,pp,'walk',companion_road=name)
    MODULES[name]['walk_companion']=wn

for width in [2,4]:
    for length in [2,4,8]:walk_strip(f'Transit_Walk_W{width}_Straight_{length}m',line_path(length),length,width)
    for radius in [4,8]:
        for angle in [45,90]:
            walk_strip(f'Transit_Walk_W{width}_Curve{angle}_R{radius}',curve_path(radius,angle),
                       radius*math.radians(angle),width,math.ceil(angle/5),radius_m=radius,turn_degrees=angle)
    # Raised pedestrian plazas with 3/4 compatible pedestrian mouths.
    for tee in [False,True]:
        h=width/2;L=width*1.5
        pts=[(-L,h),(-h,h),(-h,L),(h,L),(h,h),(L,h),(L,-h),(h,-h),(h,-L),(-h,-L),(-h,-h),(-L,-h)]
        if tee:pts=[(-L,h),(L,h),(L,-h),(h,-h),(h,-L),(-h,-L),(-h,-h),(-L,-h)]
        dirs=[(-1,0),(1,0),(0,-1)]+([] if tee else [(0,1)])
        module(f'Transit_Walk_W{width}_'+('T' if tee else 'X')+'Junction',
               [('Pavement',slab_polygon(pts,.15,.35,'Walk_Paver'))],
               [port(str(d),(d[0]*L,d[1]*L),d,width,.15,'walk') for d in dirs],'walk',walk_width_m=width)
    path=line_path(4)
    specs=[('Ramp',ribbon(Geo(),path,0,1,width,.15,.2,'Walk_Paver',4,z_end=0))]
    g=Geo()
    for j in range(width*4):g.box((1.65,-width/2+.125+j*.25,.019),(.4,.10,.012),'Walk_Tactile')
    specs.append(('Tactile',g))
    module(f'Transit_Walk_W{width}_AccessibleRamp_4m',specs,
           [port('raised',(-2,0),(-1,0),width,.15,'walk'),port('flush',(2,0),(1,0),width,0,'walk')],
           'walk',slope=0.0375,height_delta_m=.15)
    module(f'Transit_Walk_W{width}_EndCap',
           [('Pavement',Geo().box((0,0,-.025),(1,width,.35),'Walk_Paver')),
            ('Curb',Geo().box((.44,0,-.025),(.12,width,.35),'Ceramic'))],
           [port('start',(-.5,0),(-1,0),width,.15,'walk')],'walk')

for lanes in [2,4,6]:
    w=lanes*3;path=line_path(4)
    g=Geo()
    for i in range(lanes*4):g.box((0,-w/2+.18+i*.75,.014),(2.8,.36,.01),'Ceramic')
    module(f'Transit_Road_{lanes}L_Crosswalk_4m',
           [('Surface',ribbon(Geo(),path,0,1,w,0,.2,'Road_Basalt')),('MarkCrosswalk',g)],
           path_ports(path,w)+[port('walk_left',(0,w/2),(0,1),4,0,'walk'),
                               port('walk_right',(0,-w/2),(0,-1),4,0,'walk')],
           'road',lanes=lanes,pedestrian_crossing=True,
           marking_module=f'Transit_CrosswalkPaint_{lanes}L',
           surface_module=f'Transit_Road_{lanes}L_Straight_4m',walkway_is_separate=True)
    module(f'Transit_CrosswalkPaint_{lanes}L',[('MarkCrosswalk',g)],[],
           'marking',lanes=lanes,paint_only=True,collision_role='none',
           surface_module=f'Transit_Road_{lanes}L_Straight_4m',walkway_is_separate=True)

# Sparse actual geometry: no transparent sheets, tube walls, overhead frames or full-width arrows.
def airguide(name,path,length,steps=1,**meta):
    specs=[];coverage=0
    for s in [-1,1]:
        g=Geo()
        count=max(1,round(length/8))
        for i in range(count):
            t=(i+.5)/count
            ribbon(g,path,t-.325/length,t+.325/length,.035,.009,.018,'AirGuide_Subtle',1,s*3)
            coverage+=.65*.035
        specs.append((f'EdgeTicks{s}',g))
    # Two short arrow arms per 16m, strictly outside the central 5m clear corridor.
    g=Geo()
    for i in range(max(1,round(length/16))):
        t=(i+.5)/max(1,round(length/16));(x,y),(dx,dy)=path(t)
        for s in [-1,1]:
            p=Vector((x-dy*s*3.24,y+dx*s*3.24,0))
            for arm in [-1,1]:
                a=p+Vector((-dx*.28-dy*arm*.15,-dy*.28+dx*arm*.15,0))
                g.rod(a,p,.018,.018,'AirGuide_Subtle',sides=4)
                coverage+=.32*.036
    specs.append(('DirectionTicks',g))
    name=module(name,specs,path_ports(path,6,0,'air'),'airguide',path_length_m=length,
                corridor_width_m=6,central_clear_width_m=5,marker_width_m=.035,
                tick_length_m=.65,tick_spacing_target_m=8,emission_strength=.25,
                nominal_plan_coverage_ratio=coverage/(length*6),
                collision=False,altitude_placement='translate whole module to flight altitude',**meta)
    return name

airguide('Transit_AirGuide_Straight_32m',line_path(32),32)
airguide('Transit_AirGuide_Straight_16m',line_path(16),16)
for r in [32,64]:
    for a in [15,30,45,90]:airguide(f'Transit_AirGuide_Curve{a}_R{r}',curve_path(r,a),r*math.radians(a),radius_m=r,turn_degrees=a)
# Junction markers have only mouth ticks, keeping the entire center empty.
for tee in [False,True]:
    specs=[];pp=[]
    dirs=[(-1,0),(1,0),(0,-1)]+([] if tee else [(0,1)])
    for i,(dx,dy) in enumerate(dirs):
        g=Geo()
        for s in [-1,1]:
            p=(dx*7-dy*s*3,dy*7+dx*s*3,0)
            g.rod((p[0]-dx*.325,p[1]-dy*.325,0),(p[0]+dx*.325,p[1]+dy*.325,0),.018,.018,'AirGuide_Subtle',sides=4)
        specs.append((f'MouthTicks{i}',g));pp.append(port(str(i),(dx*8,dy*8),(dx,dy),6,0,'air'))
    module('Transit_AirGuide_'+('T' if tee else 'X')+'Junction',specs,pp,'airguide',
           corridor_width_m=6,central_clear_width_m=5,collision=False,marker_width_m=.035,
           nominal_plan_coverage_ratio=.001,altitude_placement='translate whole module to flight altitude')

def export(path,names,kind):
    path=ROOT/path;path.parent.mkdir(parents=True,exist_ok=True)
    bpy.context.window.scene=scene
    bpy.ops.object.select_all(action='DESELECT')
    for n in names:PROTOTYPES[n].select_set(True)
    bpy.context.view_layer.objects.active=PROTOTYPES[names[0]]
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,use_active_scene=True,
        export_animations=False,export_skins=False,export_yup=True,export_extras=True,
        export_cameras=False,export_lights=False,export_materials='EXPORT',export_texcoords=True,export_normals=True)
    EXPORTS.append(dict(file=path.relative_to(ROOT).as_posix(),type=kind,mesh_parts=names,clips=0,expansion=TAG))

for name,info in MODULES.items():
    export(f'Export/Transit/{name}.glb',info['parts'],'scenery')
    for n in info['parts']:export(f'Export/Transit/Parts/{n}.glb',[n],'module')
print('TRANSIT_EXPORTED',len(MODULES),len(ASSETS),flush=True)

def write(path,value):
    (ROOT/path).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

issues=[]
for a in ASSETS:
    o=PROTOTYPES[a['id']];o.data.calc_loop_triangles()
    for tr in o.data.loop_triangles:
        p,q,r=[o.data.vertices[i].co for i in tr.vertices]
        if (q-p).cross(r-p).length<1e-9:issues.append(a['id']+': degenerate triangle')
    assert o.data.uv_layers and 0<a['triangles']<=500
    assert all(math.isfinite(x) for v in o.data.vertices for x in v.co)
assert not issues,issues

# Retain prior expansions and their catalog entries unchanged.
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
catalog['assets']=[a for a in catalog['assets'] if a.get('expansion')!=TAG]+ASSETS
catalog['exports']=[a for a in catalog['exports'] if a.get('expansion')!=TAG]+EXPORTS
write('Documentation/asset_catalog.json',catalog)
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
recipes.update({n:dict(parts=m['parts'],expansion=TAG) for n,m in MODULES.items()})
write('Documentation/assembly_recipes.json',recipes)
write('Documentation/transit_modules.json',dict(schema='erma_transit_v1',units='meters',
    authoring_up='+Z',gltf_up='+Y',gltf_coordinate_mapping='(x,y,z) -> (x,z,-y)',
    road_surface_z_m=0,walk_surface_z_m=.15,lane_width_m=3,modules=MODULES,
    connection_rule='Match kind and width. Transform port positions equal; outward vectors opposite. No rescaling.',
    legacy_compatible=['Road_2Lane_4m','Road_4Lane_4m','Road_6Lane_4m'],
    legacy_curve_note='Legacy Road_Curve90_R9m uses arc-center origin; new curves use incoming centerline endpoint.'))
overall=json.loads((ROOT/'Validation/source_report.json').read_text(encoding='utf-8'))
overall.update(mesh_parts=len(catalog['assets']),max_triangles=max(a['triangles'] for a in catalog['assets']),
               all_parts_under_500=all(a['triangles']<=500 for a in catalog['assets']))
overall['transit_expansion_report']='transit_source_report.json'
write('Validation/source_report.json',overall)
write('Validation/transit_source_report.json',dict(expansion=TAG,modules=len(MODULES),mesh_parts=len(ASSETS),
    categories={k:sum(m['category']==k for m in MODULES.values()) for k in ['road','walk','airguide','marking']},
    max_triangles=max(a['triangles'] for a in ASSETS),all_parts_under_500=True,issues=issues,
    source_geometry_passed=True))

# Overview scenes plus a socket-assembled neighborhood; these keep the actual mesh scale.
OVERVIEW,oc=studio('14_Transit_Roads',(0,0,0),140,(2200,1600))
chosen=[f'Transit_Road_{l}L_Curve90_R18' for l in [2,4,6]]+JUNCTIONS+[
    'Transit_Road_Taper_2L_4L_24m','Transit_Road_Taper_4L_6L_24m','Transit_Road_Taper_2L_6L_36m']
for i,n in enumerate(chosen):
    xy=((i%3)*43,(i//3)*39)
    display(MODULES[n]['parts'],oc,(*xy,0))
    wn=MODULES[n].get('walk_companion')
    if wn:display(MODULES[wn]['parts'],oc,(*xy,0))
    label(oc,n.replace('Transit_Road_',''),(xy[0]+3,xy[1]-16,.02),.65)
WALKS,wc=studio('15_Transit_Pavements',(0,0,0),45,(2000,1400))
chosen=['Transit_Walk_W2_Straight_8m','Transit_Walk_W4_Straight_8m','Transit_Walk_W2_Curve90_R4',
        'Transit_Walk_W4_Curve90_R8','Transit_Walk_W2_TJunction','Transit_Walk_W4_XJunction',
        'Transit_Walk_W2_AccessibleRamp_4m','Transit_Walk_W4_AccessibleRamp_4m','Transit_Road_2L_Crosswalk_4m']
for i,n in enumerate(chosen):
    xy=((i%3)*18,(i//3)*18)
    display(MODULES[n]['parts'],wc,(*xy,0))
    label(wc,n.replace('Transit_',''),(xy[0],xy[1]-5,.01),.34)

LAYOUT,lc=studio('16_Transit_Connected_Example',(0,0,0),62,(2000,1400))
PLACEMENTS=[]
def place(n,x=0,y=0,yaw=0,z=0):
    display(MODULES[n]['parts'],lc,(x,y,z),yaw)
    PLACEMENTS.append(dict(module=n,translation_m=[x,y,z],yaw_degrees=math.degrees(yaw)))
    return len(PLACEMENTS)-1
def attach(parent,pid,n,qid,companion=True):
    par=PLACEMENTS[parent];a=next(p for p in MODULES[par['module']]['ports'] if p['id']==pid)
    b=next(p for p in MODULES[n]['ports'] if p['id']==qid)
    assert a['kind']==b['kind'] and a['width_m']==b['width_m']
    ya=math.radians(par['yaw_degrees']);v=Vector(a['outward']);da=Matrix.Rotation(ya,3,'Z')@v
    yaw=math.atan2(-da.y,-da.x)-math.atan2(b['outward'][1],b['outward'][0])
    pa=Vector(par['translation_m'])+Matrix.Rotation(ya,3,'Z')@Vector(a['position_m'])
    pos=pa-Matrix.Rotation(yaw,3,'Z')@Vector(b['position_m'])
    idx=place(n,*pos[:2],yaw,pos.z)
    if companion and MODULES[n].get('walk_companion'):place(MODULES[n]['walk_companion'],*pos[:2],yaw,pos.z)
    CONNECTIONS.append(dict(a=parent,port_a=pid,b=idx,port_b=qid))
    return idx
CONNECTIONS=[]
n='Transit_Road_2L_XJunction_R4';center=place(n)
place(MODULES[n]['walk_companion'])
for pid,angle in [('road_1_0',90),('road_-1_0',45),('road_0_-1',30),('road_0_1',15)]:
    c=attach(center,pid,f'Transit_Road_2L_Curve{angle}_R18','start')
    d=attach(c,'end','Transit_Road_2L_Straight_8m','start')
    par=PLACEMENTS[d]
    # Straight side pavements have exact matching curve-end cross sections.
    yaw=math.radians(par['yaw_degrees'])
    for s in [-1,1]:
        off=Matrix.Rotation(yaw,3,'Z')@Vector((0,s*4,0))
        place('Transit_Walk_W2_Straight_8m',par['translation_m'][0]+off.x,par['translation_m'][1]+off.y,yaw+(math.pi if s<0 else 0))
# A separate crossing sample proves the raised-to-flush pedestrian route in both directions.
crossing=place('Transit_Road_2L_Crosswalk_4m',38,-17)
for pid in ['walk_left','walk_right']:
    ramp=attach(crossing,pid,'Transit_Walk_W4_AccessibleRamp_4m','flush',False)
    walk=attach(ramp,'raised','Transit_Walk_W4_Straight_2m','start',False)
    attach(walk,'end','Transit_Walk_W4_EndCap','start',False)
write('Documentation/transit_layout_example.json',dict(units='meters',placements=PLACEMENTS,connections=CONNECTIONS))

AIR,ac=studio('17_Transit_AirGuides',(0,0,0),90,(2000,1300))
for n,xy in [('Transit_AirGuide_Straight_32m',(0,0)),('Transit_AirGuide_Curve90_R32',(20,0)),
             ('Transit_AirGuide_XJunction',(-23,18)),('Transit_AirGuide_Curve45_R32',(-24,-20))]:
    display(MODULES[n]['parts'],ac,(*xy,2))
    label(ac,n.replace('Transit_AirGuide_',''),(xy[0],xy[1]-5,.02),.55)
# View at human height: marker meshes cannot form a ceiling or obscure the skyline.
EYE,ec=studio('18_Transit_AirGuide_PlayerView',(0,0,0),30,(1800,1000))
for k in range(4):display(MODULES['Transit_AirGuide_Straight_32m']['parts'],ec,(k*32,0,7))
for n,x,y in [('Transit_Road_2L_Straight_16m',6,0),('Transit_Walk_W2_Straight_8m',6,-4)]:
    display(MODULES[n]['parts'],ec,(x,y,0))
# Reuse existing settlement silhouettes for a readable visual reference; display-only copies.
for n,xy in [('City_TransitShelter',(18,8)),('Utility_CargoContainer_6m',(28,-10)),('Utility_CommRelay',(43,12))]:
    data=json.loads((ROOT/'Documentation/scenery_assemblies.json').read_text(encoding='utf-8'))['assemblies'][n]
    display(data['parts'],ec,(*xy,0))
EYE.camera.data.type='PERSP';EYE.camera.data.lens=24
EYE.camera.location=(-9,-1.5,1.65)
EYE.camera.rotation_euler=(Vector((36,0,7))-EYE.camera.location).to_track_quat('-Z','Y').to_euler()
for s in [OVERVIEW,WALKS,LAYOUT,AIR,EYE]:s.view_settings.exposure=.7
for s in [OVERVIEW,WALKS,LAYOUT,AIR]:fit_camera(s)
bpy.context.window.scene=LAYOUT
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA'
bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE),compress=True)
print('TRANSIT_SAVED',flush=True)
if '--no-render' not in sys.argv:
    for s,file in [(OVERVIEW,'18_transit_roads'),(WALKS,'19_transit_pavements'),
                   (LAYOUT,'20_transit_connected'),(AIR,'21_air_guides'),(EYE,'22_air_guide_player_view')]:
        bpy.context.window.scene=s;s.render.filepath=str(ROOT/'Preview'/f'{file}.png')
        bpy.ops.render.render(write_still=True)
        print('TRANSIT_PREVIEW',file,flush=True)
print('TRANSIT_COMPLETE',flush=True)
