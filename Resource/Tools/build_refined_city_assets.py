"""Extend the saved Blender library: detailed glazing, legacy refinements and five architectural styles.

Run after the air/city and transit builders. Writes only Resource deliverables.
"""
import ast
import json
import math
import sys
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector, Matrix, Quaternion

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'Source/Erma_Resource_Library.blend'
TAG='resource_refinement_v1'
REVISION='framed_glazing_architecture_v1'
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
entry_scene=bpy.context.scene.name
bpy.context.preferences.filepaths.save_version=0
before_ids=['Vehicle_Coupe_Canopy_Panoramic','Aircraft_Warden_Survey_Canopy_Glass','Hab_Wall_Window_2m']
before={n:(bpy.data.objects.get('Refinement_Before_'+n) or bpy.data.objects[n]).data.copy() for n in before_ids}
for s in list(bpy.data.scenes):
    if s.get('expansion')==TAG:bpy.data.scenes.remove(s)
for o in list(bpy.data.objects):
    if o.get('expansion')==TAG:bpy.data.objects.remove(o,do_unlink=True)
for c in list(bpy.data.collections):
    if c.get('expansion')==TAG:bpy.data.collections.remove(c)
scene=bpy.data.scenes['00_Library']
bpy.context.window.scene=scene
scene.frame_set(0)
MATS={m.name:m for m in bpy.data.materials}

def material(name,color,rough=.4,metal=.3,alpha=1,emission=0):
    m=MATS.get(name) or bpy.data.materials.new(name)
    m.use_nodes=True
    m.diffuse_color=(*color,alpha)
    p=m.node_tree.nodes.get('Principled BSDF')
    for k,v in {'Base Color':(*color,1),'Metallic':metal,'Roughness':rough,'Alpha':alpha,
                'Emission Color':(*color,1),'Emission Strength':emission}.items():p.inputs[k].default_value=v
    m.use_backface_culling=False
    MATS[name]=m
    return m

material('Glazing_Laminated',(.10,.36,.43),.16,.38,.63)
material('Glazing_Armored',(.055,.19,.25),.23,.52,.80)
material('Glazing_Solar',(.11,.21,.32),.26,.55,.76)
material('Facade_Porcelain',(.69,.76,.73),.36,.42)
material('Facade_DeepTeal',(.035,.18,.21),.39,.55)
material('Facade_Terracotta',(.42,.20,.13),.57,.18)
material('Frame_Graphite',(.037,.055,.067),.36,.65)

tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='Geo')],type_ignores=[]),'<base mesh>','exec'))
LegacyGeo=Geo
sys.path.insert(0,str(ROOT/'Tools'))
from detail_mesh_pipeline import DetailGeometry,DetailPipeline

def collection(name,parent=None):
    c=bpy.data.collections.get(name)
    if c:return c
    c=bpy.data.collections.new(name);c['expansion']=TAG
    (parent or scene.collection).children.link(c)
    return c

LIB=collection('19_Refined_City',bpy.data.collections['RESOURCE_LIBRARY'])
rawscene=bpy.data.scenes.new('24_Refined_Detail_Sources');rawscene['expansion']=TAG
rawcol=bpy.data.collections.new('Refined_Editable_Sources');rawcol['expansion']=TAG
rawscene.collection.children.link(rawcol)
PIPE=DetailPipeline(rawcol,MATS,TAG,allow_draft_over_budget=True)
ASSETS=[];PROTOTYPES={};COMPANIONS={};BUILDINGS={};new_exports=[]
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
oldassets={a['id']:a for a in catalog['assets']}

def adopt(g,name,c,rig=None,slot='module',family='architecture'):
    old=bpy.data.objects.get(name)
    instances=[o for o in bpy.data.objects if o.type=='MESH' and o.get('asset_id')==name
               and o.get('role')!='editable_detail_source_not_exported'] if old else []
    temp,a=PIPE.build(g,name,c,rig,slot,family)
    raw=bpy.data.objects[PIPE.records[-1]['source_object']]
    raw.name='RefinedDetail__'+name
    a['detail_source']=raw.name;PIPE.records[-1]['source_object']=raw.name
    a={**oldassets.get(name,{}),**a,'refinement_revision':REVISION}
    if old:
        weights=[[(temp.vertex_groups[x.group].name,x.weight) for x in v.groups] for v in temp.data.vertices]
        group_names=[x.name for x in temp.vertex_groups]
        for o in instances:
            o.data=temp.data.copy()
            o.vertex_groups.clear()
            if any(m.type=='ARMATURE' for m in o.modifiers) or o==old:
                groups={n:o.vertex_groups.new(name=n) for n in group_names}
                for vi,items in enumerate(weights):
                    for bn,w in items:groups[bn].add([vi],w,'REPLACE')
            o['triangle_count']=a['triangles'];o['detail_source']=raw.name;o['refinement_revision']=REVISION
        bpy.data.objects.remove(temp,do_unlink=True)
        obj=old
    else:
        obj=temp;obj['expansion']=TAG;a['expansion']=TAG
    obj['pipeline']='detail_first_lossless_cleanup_v1'
    ASSETS.append(a);PROTOTYPES[name]=obj
    return obj

def beam(g,a,b,width,depth,mat,bone=None):
    a,b=Vector(a),Vector(b);d=b-a
    rot=d.to_track_quat('Z','Y').to_matrix()
    g.box((a+b)/2,(width,depth,d.length),mat,bone,rot)

def pane(g,points,mat,bone=None,thick=.009):
    pts=[Vector(p) for p in points]
    n=(pts[1]-pts[0]).cross(pts[2]-pts[0]).normalized()*thick/2
    count=len(pts)
    verts=[p-n for p in pts]+[p+n for p in pts]
    faces=[tuple(reversed(range(count))),tuple(range(count,2*count))]
    faces += [(i,(i+1)%count,(i+1)%count+count,i+count) for i in range(count)]
    return g.add(verts,faces,mat,bone)

def inset(points,f=.025):
    pts=[Vector(p) for p in points];center=sum(pts,Vector())/len(pts)
    return [p+(center-p).normalized()*f for p in pts]

def canopy(sections,paint,bone='canopy',armored=False,bike=False):
    frame,glass=Geo(),Geo()
    rings=[]
    for x,w,bottom,top in sections:
        rings.append([Vector((x,-w,bottom)),Vector((x,-w*.88,top)),Vector((x,w*.88,top)),Vector((x,w,bottom))])
    # Open structural cage: thin seal strips behind load-bearing rails; no glass volume filling the cabin.
    edges=set()
    for i in range(len(rings)):
        for j,k in [(0,1),(1,2),(2,3),(3,0)]:
            if i in [0,len(rings)-1] or (j,k)==(1,2):edges.add((i,j,i,k))
    for i in range(len(rings)-1):
        for j in range(4):edges.add((i,j,i+1,j))
        for j,k in [(0,1),(1,2),(2,3)]:
            corners=[rings[i][j],rings[i+1][j],rings[i+1][k],rings[i][k]]
            pane(glass,inset(corners,.038 if armored else .027),'Glazing_Armored' if armored else 'Glazing_Laminated',bone)
    for i in [0,len(rings)-1]:pane(glass,inset(rings[i],.03),'Glazing_Laminated',bone)
    for i,j,k,l in sorted(edges):
        a,b=rings[i][j],rings[k][l]
        beam(glass,a,b,.048 if armored else .032,.042,'Rubber',bone)
        # Raised metallic edge remains distinct from the gasket in grazing light.
        beam(frame,a+Vector((0,0,.012)),b+Vector((0,0,.012)),.027 if armored else .018,.024,paint if armored else 'Alloy_Light',bone)
    # Wiper and hinge geometry are separate visible details, rigid to the opening canopy.
    a,b=rings[-1][1],rings[-1][2]
    beam(frame,a.lerp(b,.15),a.lerp(b,.64)+Vector((.009,0,.02)),.014,.012,'Frame_Graphite',bone)
    if bike:beam(frame,(sections[0][0],-.22,sections[0][2]),(sections[0][0],.22,sections[0][2]),.025,.025,'Steel',bone)
    return frame,glass

def register_pair(name,frame,glass,c,rig,slot,family):
    gid=name+'_Glazing'
    adopt(glass,gid,c,rig,slot+'.glazing',family)
    obj=adopt(frame,name,c,rig,slot,family)
    COMPANIONS[name]=gid
    obj['glazing_companion']=gid
    obj['assembly_rule']='include frame and its glazing companion; same attachment bone'
    return obj

class Geo(DetailGeometry,LegacyGeo):
    def make(self,name,c,rig=None,slot=None,family=None):
        if name=='Vehicle_Shuttle_Payload_Passenger':
            p=vehicle_params['Shuttle'];L,W,H=p['length'],p['width'],p['height'];z=.18
            cx=-.29*L;left=cx-.11*L;right=cx+.11*L;y=.345*W
            g=Geo().box((cx,0,z+.46*H),(.22*L,.69*W,.30*H),p['paint'],'chassis')
            g.box((cx,0,z+.94*H),(.22*L,.69*W,.07*H),p['paint'],'chassis')
            for x in [left+.025,cx,right-.025]:
                for s in [-1,1]:g.box((x,s*y,z+.76*H),(.045,.055,.32*H),'Alloy_Light','chassis')
            for x in [left+.018,right-.018]:g.box((x,0,z+.76*H),(.035,.66*W,.32*H),'Frame_Graphite','chassis')
            glass=Geo()
            for s in [-1,1]:
                for x in [(left+cx)/2,(right+cx)/2]:
                    glass.box((x,s*(y-.008),z+.76*H),(.105*L-.05,.012,.30*H),'Glazing_Laminated','chassis')
                    for zz in [.61,.91]:glass.box((x,s*(y+.008),z+zz*H),(.105*L-.05,.016,.018),'Rubber','chassis')
            return register_pair(name,g,glass,c,rig,slot,family)
        if name.startswith('Vehicle_') and '_Canopy_' in name:
            kind=name.split('_')[1];p=vehicle_params[kind]
            L,W,H=p['length'],p['width'],p['height'];z=.18
            bike=kind=='JetBike';armored='Armored' in name
            if bike:
                wide='Rally' in name
                sections=[(.12*L,W*(.17 if wide else .10),z+H*.54,z+H*.60),
                          (.25*L,W*(.20 if wide else .14),z+H*.62,z+H*.96),
                          (.30*L,W*(.11 if wide else .08),z+H*.66,z+H*.83)]
            else:
                rear=-.22*L if kind!='Hauler' else .05*L;front=.22*L if kind!='Hauler' else .35*L
                hw=W*.35
                sections=[(rear,hw*.85,z+H*.50,z+H*.64),(rear+.08*L,hw*.88,z+H*.56,z+H*.96),
                          (front-.07*L,hw*.75,z+H*.56,z+H*(.93 if armored else 1)),(front,hw*.57,z+H*.54,z+H*.63)]
            frame,glass=canopy(sections,p['paint'],armored=armored,bike=bike)
            return register_pair(name,frame,glass,c,rig,slot,family)
        return adopt(self,name,c,rig,slot,family)

# Rebuild the old five vehicles with manufactured edge/curve detail while preserving rigs and action names.
vehicle_meta=json.loads((ROOT/'Documentation/vehicle_assemblies.json').read_text(encoding='utf-8'))
vehicle_params={k:v['parameters'] for k,v in vehicle_meta['types'].items()}
def armature(name,specs,c):return bpy.data.objects[name]
tree=ast.parse((ROOT/'Tools/vehicle_design.py').read_text(encoding='utf-8'))
nodes=[]
for node in tree.body:
    if isinstance(node,ast.FunctionDef) and node.name=='animate_vehicle':break
    nodes.append(node)
exec(compile(ast.Module(body=nodes,type_ignores=[]),'<existing vehicle design>','exec'))

# Aircraft use the established induction propulsion and the existing opening joints.
airmeta=json.loads((ROOT/'Documentation/aircraft_assemblies.json').read_text(encoding='utf-8'))
for kind,info in airmeta['types'].items():
    p=info['parameters'];L,W,H=p['length'],p['span'],p['body_height']
    z=1.05 if kind=='Mule_Skycrane' else .48;hw=W*(.075 if kind in ['Kite_Courier','Warden_Survey'] else .12)
    sections=[(-.07*L,hw*.89,z+.50*H,z+.73*H),(.03*L,hw*.90,z+.54*H,z+H),
              (.22*L,hw*.68,z+.50*H,z+.87*H),(.30*L,hw*.31,z+.44*H,z+.54*H)]
    rig=bpy.data.objects[info['skeleton']]
    for name in info['slots']['canopy']:
        old=bpy.data.objects[name];reinforced='Reinforced' in name
        frame,glass=canopy(sections,p['paint'],armored=reinforced)
        register_pair(name,frame,glass,old.users_collection[0],rig,'canopy','aircraft.'+kind)

# Selected old building and interior parts: real apertures, recessed glazing and service details.
c=collection('Legacy_Refined_Parts',LIB)
g=Geo().box((0,0,.73),(2,.18,1.46),'Habitat_Panels').box((0,0,2.91),(2,.18,1.38),'Habitat_Panels')
for x in [-.90,.90,0]:g.box((x,-.015,1.84),(.20 if x else .045,.20,.76),'Steel')
for z in [1.46,2.22]:g.box((0,-.025,z),(1.76,.23,.055),'Alloy_Light')
for x in [-.42,.42]:
    for z in [1.49,2.19]:g.box((x,-.143,z),(.75,.02,.018),'Rubber')
g.box((0,-.15,2.25),(1.7,.025,.025),'Glow_Amber')
glass=Geo()
for x in [-.425,.425]:glass.box((x,-.04,1.84),(.79,.012,.68),'Glazing_Laminated')
register_pair('Hab_Wall_Window_2m',g,glass,c,None,'wall','architecture')
g=Geo()
for z in [-.17,.17]:g.box((0,0,z),(1.8,.12,.06),'Steel')
for x in [-.86,.86,0]:g.box((x,0,0),(.08 if x else .025,.12,.30),'Steel')
for x in [-.43,.43]:g.box((x,-.018,0),(.80,.012,.27),'Glazing_Armored')
g.box((0,-.072,-.14),(1.65,.02,.018),'Glow_Cyan')
adopt(g,'Hab_Slit_Window',c,None,'window','architecture')
g=Geo().box((0,0,.11),(.55,.48,.22),'Steel').box((0,.05,.55),(.20,.22,.72),'Steel')
g.box((0,0,1.22),(.76,.16,.55),'Frame_Graphite').box((0,-.089,1.24),(.64,.012,.40),'Glazing_Armored')
for i in range(4):g.box((-.20+i*.13,-.102,1.33-i*.035),(.09,.007,.012),'Glow_Cyan')
g.box((0,-.22,.94),(.50,.29,.055),'Alloy_Light')
for i in range(4):g.box((-.16+i*.10,-.28,.973),(.06,.045,.006),'Glow_Cyan')
for s in [-1,1]:g.box((s*.13,.06,.55),(.02,.22,.28),'Glow_Cyan')
adopt(g,'Interior_Terminal',c,None,'interior.terminal','architecture')
# The old crossing ID now represents only the road substrate. Paint is an explicit companion.
adopt(Geo().box((0,0,-.09),(2,2,.18),'Road_Basalt'),'Road_Crossing_2m',c,None,'road','environment')
paint=Geo()
for i in range(5):paint.box((0,-.75+i*.38,.012),(1.65,.16,.024),'Ceramic')
adopt(paint,'Road_Crossing_Paint_2m',c,None,'road.marking','environment')
COMPANIONS['Road_Crossing_2m']='Road_Crossing_Paint_2m'

# Add companion geometry to every saved assembled or posed copy, not just to canonical prototypes.
for name,gid in COMPANIONS.items():
    source=bpy.data.objects[gid];canonical=bpy.data.objects[name]
    for instance in list(bpy.data.objects):
        if instance==canonical or instance.type!='MESH' or instance.get('asset_id')!=name:continue
        if instance.get('role')=='editable_detail_source_not_exported':continue
        ob=source.copy();ob.data=source.data;ob.name=instance.name+'__Glazing'
        ob['expansion']=TAG;ob.asset_clear();ob.parent=instance.parent
        ob.matrix_basis=instance.matrix_basis.copy()
        for m in ob.modifiers:
            if m.type=='ARMATURE':m.object=next((x.object for x in instance.modifiers if x.type=='ARMATURE'),None)
        if not any(m.type=='ARMATURE' for m in instance.modifiers):
            ob.data=source.data.copy();ob.modifiers.clear();ob.vertex_groups.clear()
        for target in instance.users_collection:target.objects.link(ob)

def expanded(names):
    out=[]
    for name in names:
        if name.endswith('_Glazing') and name not in bpy.data.objects:continue
        if name not in out:out.append(name)
        if name in COMPANIONS and COMPANIONS[name] not in out:out.append(COMPANIONS[name])
    return out

for kind,info in vehicle_meta['types'].items():
    for key in ['default_parts','alternate_parts']:info[key]=expanded(info[key])
    info['glazing_companions']={n:g for n,g in COMPANIONS.items() if n.startswith('Vehicle_'+kind+'_')}
for kind,info in airmeta['types'].items():
    for key in ['parts','default_parts','alternate_parts']:info[key]=expanded(info[key])
    info['glazing_companions']={n:g for n,g in COMPANIONS.items() if n.startswith('Aircraft_'+kind+'_')}

# New architecture kit. Each module is an actual facade, slab, brace, roof or service unit.
ac=collection('Architecture_New_Style_Modules',LIB)
def module(name,g,slot):return adopt(g,'Arch_'+name,ac,None,slot,'architecture.new_styles').name
frame=Geo()
frame.box((0,0,.25),(3,.24,.50),'Facade_DeepTeal').box((0,0,3.45),(3,.24,.30),'Facade_Porcelain')
for x in [-1.44,1.44,0]:frame.box((x,0,1.90),(.12 if x else .055,.24,2.80),'Frame_Graphite')
for z in [.55,3.28]:frame.box((0,-.13,z),(2.82,.035,.025),'Rubber')
frame.box((0,-.15,.31),(2.65,.025,.025),'Glow_Cyan')
module('Facade_Frame_3m',frame,'facade.frame')
glass=Geo()
for x in [-.72,.72]:glass.box((x,-.035,1.92),(1.35,.014,2.65),'Glazing_Laminated')
module('Facade_Glazing_3m',glass,'facade.glazing')
module('Facade_Porcelain_Fin',Geo().box((0,-.08,1.80),(.24,.65,3.6),'Facade_Porcelain').box((0,-.425,1.8),(.05,.018,2.85),'Glow_Cyan'),'facade.fin')
module('Facade_Terracotta_Fin',Geo().box((0,-.08,1.80),(.32,.52,3.6),'Facade_Terracotta').box((0,-.35,1.8),(.05,.016,2.7),'Copper'),'facade.fin')
module('Storey_Column',Geo().box((0,0,1.8),(.38,.38,3.6),'Steel').box((0,-.20,1.8),(.10,.025,2.8),'Glow_Cyan'),'structure.column')
module('Floor_Slab_Unit',Geo().box((0,0,-.14),(1,1,.28),'Facade_Porcelain'),'structure.floor')
module('Plinth_Unit',Geo().box((0,0,.32),(1,1,.64),'Frame_Graphite'),'structure.plinth')
module('Roof_Coping_Unit',Geo().box((0,0,.15),(1,1,.30),'Facade_DeepTeal'),'roof.coping')
g=Geo().box((0,0,-.1),(3,2,.20),'Facade_Porcelain')
for x in [-1.44,0,1.44]:g.box((x,-.95,.52),(.035,.045,1.05),'Steel')
for z in [.12,1.02]:g.box((0,-.95,z),(3,.045,.035),'Alloy_Light')
for x in [-.72,.72]:g.box((x,-.94,.57),(1.38,.014,.82),'Glazing_Armored')
module('Balcony_3m',g,'terrace.balcony')
g=Geo().box((0,0,.25),(2.8,.75,.5),'Facade_Terracotta')
for i in range(5):g.ellipsoid((-.96+i*.48,0,.66),(.29,.27,.40),'Alien_Leaf',sides=8,rings=3)
module('Planter_3m',g,'terrace.planter')
module('Roof_Service_Pod',Geo().box((0,0,.8),(2,1.2,1.6),'Facade_Porcelain').box((0,-.62,.83),(1.7,.035,1.15),'Frame_Graphite'),'roof.service')
g=Geo()
for z in [.4,.65,.9,1.15]:g.box((0,-.65,z),(1.55,.075,.065),'Alloy_Light')
module('Roof_Service_Louvres',g,'roof.louvres')
g=Geo().box((0,0,.9),(2,.08,1.8),'Glazing_Solar')
for x in [-.95,0,.95]:g.box((x,-.05,.9),(.035,.04,1.8),'Alloy_Light')
for z in [.04,.48,.92,1.36,1.76]:g.box((0,-.05,z),(1.9,.025,.018),'Alloy_Light')
module('Solar_Screen_2m',g,'roof.solar')
g=Geo().box((0,0,1.4),(2.4,.18,2.8),'Frame_Graphite')
for x in [-.57,.57]:g.box((x,-.105,1.42),(1.03,.012,2.55),'Glazing_Armored')
for x in [-1.28,1.28]:g.box((x,-.1,1.5),(.15,.45,3),'Facade_Porcelain')
g.box((0,-.16,2.97),(2.7,.35,.15),'Glow_Cyan')
module('Entry_Airlock',g,'entry')
g=Geo()
beam(g,(-1.45,0,0),(1.45,0,3.6),.17,.22,'Facade_Porcelain')
beam(g,(-1.45,.04,.1),(1.45,.04,3.5),.035,.04,'Glow_Cyan')
module('Diagonal_ExoBrace',g,'structure.brace')
# Curved vault ribs are genuine segmented profiles with separate glazing bays.
for segment in range(6):
    a=math.pi*segment/6;b=math.pi*(segment+1)/6
    points=[(6*math.cos(t),y,4*math.sin(t)) for y in [-1.5,1.5] for t in [a,b]]
    g=Geo();pane(g,[points[0],points[1],points[3],points[2]],'Glazing_Solar',thick=.016)
    module(f'Vault_Glazing_{segment}',g,'roof.vault_glazing')
    g=Geo();beam(g,points[0],points[1],.10,.12,'Facade_Porcelain');beam(g,points[2],points[3],.10,.12,'Facade_Porcelain')
    module(f'Vault_Rib_{segment}',g,'roof.vault_rib')
g=Geo()
for i in range(12):
    a=math.tau*i/12;b=math.tau*(i+1)/12
    beam(g,(3*math.cos(a),3*math.sin(a),0),(3*math.cos(b),3*math.sin(b),0),.055,.08,'Glow_Cyan')
module('Crown_Induction_Halo',g,'roof.crown')

def building(name,floors,style):
    c=collection('Prefab_'+name,LIB)
    root=bpy.data.objects.new(name,None);c.objects.link(root);root['expansion']=TAG
    root['floor_count']=floors;root['storey_height_m']=3.6;root['style']=style
    info=dict(root=root,collection=c,parts=[],floor_count=floors,storey_height_m=3.6,style=style)
    BUILDINGS[name]=info
    return info

def place(info,part,pos=(0,0,0),yaw=0,scale=(1,1,1),floor=None):
    name=part if part.startswith('Arch_') else 'Arch_'+part
    src=PROTOTYPES.get(name) or bpy.data.objects[name]
    ob=src.copy();ob.data=src.data;ob.asset_clear();ob.name=info['root'].name+'__'+part
    ob['expansion']=TAG;ob.parent=info['root'];ob.location=pos;ob.rotation_euler[2]=yaw;ob.scale=scale
    if floor is not None:ob['floor_index']=floor
    info['collection'].objects.link(ob)
    info['parts'].append(dict(part=name,position_m=list(pos),yaw_degrees=math.degrees(yaw),scale=list(scale),floor_index=floor))
    return ob

def storey(info,level,w,d,center=(0,0),yaw=0,fin='Facade_Porcelain_Fin',braces=False):
    z=level*3.6+.64;rot=Matrix.Rotation(yaw,3,'Z')
    def xyz(x,y,dz=0):return tuple(Vector((*center,0))+rot@Vector((x,y,z+dz)))
    place(info,'Floor_Slab_Unit',xyz(0,0),yaw,(w+.55,d+.55,1),level+1)
    for side in [-1,1]:
        for i in range(round(w/3)):
            x=-w/2+1.5+3*i;angle=yaw+(0 if side==-1 else math.pi)
            for part in ['Facade_Frame_3m','Facade_Glazing_3m']:place(info,part,xyz(x,side*d/2),angle,floor=level+1)
            if i%2==0:place(info,fin,xyz(x-1.5,side*d/2),angle,floor=level+1)
            if braces and i%3==0:place(info,'Diagonal_ExoBrace',xyz(x,side*(d/2+.22)),angle,floor=level+1)
        for i in range(round(d/3)):
            y=-d/2+1.5+3*i;angle=yaw+(math.pi/2 if side==1 else -math.pi/2)
            for part in ['Facade_Frame_3m','Facade_Glazing_3m']:place(info,part,xyz(side*w/2,y),angle,floor=level+1)
        for x in [-w/2,w/2]:place(info,'Storey_Column',xyz(x,side*d/2),yaw,floor=level+1)

def roof(info,z,w,d,center=(0,0),yaw=0,halo=False):
    place(info,'Roof_Coping_Unit',(*center,z),yaw,(w+.7,d+.7,1))
    for x in [-2,2]:
        place(info,'Roof_Service_Pod',(center[0]+x,center[1],z+.3))
        place(info,'Roof_Service_Louvres',(center[0]+x,center[1],z+.3))
        place(info,'Solar_Screen_2m',(center[0]+x,center[1]+2,z+.3),-.20)
    if halo:place(info,'Crown_Induction_Halo',(*center,z+2.3))

p=building('Spire_Helix_12F',12,'rotating porcelain exoskeleton / three twisting four-storey tiers')
place(p,'Plinth_Unit',(0,0,.0),scale=(15.8,15.8,1))
for i in range(12):storey(p,i,12,12,yaw=math.radians((i//4)*12),braces=True)
roof(p,43.84,12,12,yaw=math.radians(24),halo=True)
place(p,'Entry_Airlock',(0,-6.3,.64))
p=building('Tower_TwinBridge_16F',16,'paired slender towers / elevated inhabited bridges')
for cx in [-6,6]:
    place(p,'Plinth_Unit',(cx,0,0),scale=(7.5,10.5,1))
    for i in range(16):storey(p,i,6,9,(cx,0),braces=i%4==0)
    roof(p,58.24,6,9,(cx,0),halo=True)
    place(p,'Entry_Airlock',(cx,-4.7,.64))
for i in [5,11]:
    storey(p,i,6,3,(0,0),fin='Facade_Porcelain_Fin')
    place(p,'Roof_Coping_Unit',(0,0,(i+1)*3.6+.64),scale=(6,3.6,1))
p=building('Arcology_Terrace_10F',10,'stepped habitat with planted terraces and terracotta sun fins')
place(p,'Plinth_Unit',(0,0,0),scale=(19,16,1))
for i in range(10):
    tier=i//4;w,d=18-tier*3,15-tier*3;cy=tier*1.5
    storey(p,i,w,d,(0,cy),fin='Facade_Terracotta_Fin')
    if i in [3,7]:
        for x in range(-6,7,3):
            place(p,'Balcony_3m',(x,-d/2-1,3.6*(i+1)+.64),floor=i+1)
            place(p,'Planter_3m',(x,-d/2-1,3.6*(i+1)+.64),floor=i+1)
roof(p,36.64,12,9,(0,3),halo=True);place(p,'Entry_Airlock',(0,-7.7,.64))
p=building('Civic_TransitAtrium_4F',4,'arched glazed civic nave between civic service wings')
place(p,'Plinth_Unit',(0,0,0),scale=(23,16,1))
for x in [-7.5,7.5]:
    for i in range(4):storey(p,i,3,15,(x,0))
    roof(p,15.04,3,15,(x,0))
for y in [-6,-3,0,3,6]:
    for segment in range(6):
        for prefix in ['Vault_Glazing_','Vault_Rib_']:place(p,prefix+str(segment),(0,y,11.44))
for x in [-3,0,3]:place(p,'Entry_Airlock',(x,-7.6,.64))
p=building('Habitat_Courtyard_3F',3,'open central garden court / shaded residential galleries')
place(p,'Plinth_Unit',(0,0,0),scale=(20,17,1))
for center,w,d in [((0,-6),18,3),((0,6),18,3),((-7.5,0),3,9),((7.5,0),3,9)]:
    for i in range(3):storey(p,i,w,d,center,fin='Facade_Terracotta_Fin')
    roof(p,11.44,w,d,center)
for x in [-3,0,3]:
    for y in [-2,2]:place(p,'Planter_3m',(x,y,.64))
for i in [1,2]:
    for x in [-6,-3,0,3,6]:place(p,'Balcony_3m',(x,-3.5,i*3.6+.64),math.pi,floor=i+1)
place(p,'Entry_Airlock',(0,-7.7,.64))

over=[(r['id'],r['optimized_triangles']) for r in PIPE.records if r['optimized_triangles']>500]
summary=dict(parts=len(PIPE.records),source_triangles=sum(r['source_triangles'] for r in PIPE.records),
             optimized_triangles=sum(r['optimized_triangles'] for r in PIPE.records),
             max_triangles=max(r['optimized_triangles'] for r in PIPE.records),over_budget=over)
print('REFINEMENT_GEOMETRY',json.dumps(summary),flush=True)
if over:raise RuntimeError('Functional module adjustment required: '+str(over))
if '--geometry-check' in sys.argv:raise SystemExit(0)

def write(path,data):
    p=ROOT/path;t=p.with_suffix(p.suffix+'.new')
    t.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');t.replace(p)

def reset(rig):
    for b in rig.pose.bones:b.location=(0,0,0);b.rotation_mode='QUATERNION';b.rotation_quaternion=(1,0,0,0);b.scale=(1,1,1)

def export(path,objects,rig=None,animated=False):
    bpy.context.window.scene=scene
    for o in scene.objects:o.select_set(False)
    for o in objects:o.select_set(True)
    if rig:
        rig.select_set(True);bpy.context.view_layer.objects.active=rig
        if rig.animation_data:
            rig.animation_data.action=None
            for t in rig.animation_data.nla_tracks:t.mute=not animated
        reset(rig)
    scene.frame_set(0);bpy.context.view_layer.update()
    p=ROOT/str(path).replace('\\','/');p.parent.mkdir(parents=True,exist_ok=True)
    stage=p.with_name(p.stem+'.staging.glb')
    bpy.ops.export_scene.gltf(filepath=str(stage),export_format='GLB',use_selection=True,use_active_scene=True,
        export_animations=animated,export_animation_mode='NLA_TRACKS',export_nla_strips=True,
        export_force_sampling=True,export_frame_range=False,export_skins=bool(rig),export_def_bones=False,
        export_yup=True,export_extras=True,export_cameras=False,export_lights=False,
        export_materials='EXPORT',export_apply=False,export_texcoords=True,export_normals=True,export_anim_slide_to_zero=True)
    stage.replace(p)
    if rig:
        if rig.animation_data:
            for t in rig.animation_data.nla_tracks:t.mute=True
        reset(rig)

recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
for name,info in recipes.get('buildings',{}).items():
    info['parts']=[p for p in info['parts'] if p['part'] not in set(COMPANIONS.values())]
    info['parts']=[q for p in info['parts'] for q in ([p,{**p,'part':COMPANIONS[p['part']]}] if p['part'] in COMPANIONS else [p])]
for info in recipes.values():
    if isinstance(info,dict) and info.get('parts') and isinstance(info['parts'][0],str):info['parts']=expanded(info['parts'])
for kind,info in recipes.get('vehicles',{}).items():
    for key in ['parts','alternate_parts']:info[key]=expanded(info[key])
    info['glazing_companions']=vehicle_meta['types'][kind]['glazing_companions']

changed=set(PROTOTYPES)
exports=[]
for e in catalog['exports']:
    if e.get('expansion')==TAG:continue
    e={**e,'file':e['file'].replace('\\','/')}
    names=e['mesh_parts']
    if any(n in changed for n in names):
        if e['type']=='building':
            name=Path(e['file']).stem;c=bpy.data.collections['Prefab_'+name]
            objects=[o for o in c.objects if o.type=='MESH']
            e['mesh_parts']=[o['asset_id'] for o in objects]
            export(e['file'],objects)
        else:
            # Parts exports remain one independently swappable mesh per file.
            # Completed assemblies include each frame's paired glazing module.
            e['mesh_parts']=([n for n in names if n not in set(COMPANIONS.values())]
                             if e['type']=='module' else expanded(names))
            rig_name=next((oldassets.get(n,{}).get('skeleton') for n in names if oldassets.get(n,{}).get('skeleton')),None)
            rig=bpy.data.objects.get(rig_name) if rig_name else None
            export(e['file'],[bpy.data.objects[n] for n in e['mesh_parts']],rig,bool(e['clips']))
    exports.append(e)

existingfiles={e['file'] for e in exports}
for a in ASSETS:
    if a['id'] in oldassets and oldassets[a['id']].get('expansion')!=TAG:continue
    name=a['id'];folder='Vehicles/Parts' if name.startswith('Vehicle_') else 'Aircraft/Parts' if name.startswith('Aircraft_') else 'Buildings/Parts' if name.startswith('Arch_') else 'Environment'
    file=f'Export/{folder}/{name}.glb'
    rig=bpy.data.objects.get(a['skeleton']) if a['skeleton'] else None
    export(file,[PROTOTYPES[name]],rig)
    exports.append(dict(file=file,type='module',mesh_parts=[name],clips=0,expansion=TAG))
for name,info in BUILDINGS.items():
    file=f'Export/Buildings/{name}.glb';objects=list(info['collection'].objects)
    export(file,objects)
    exports.append(dict(file=file,type='building',mesh_parts=[o['asset_id'] for o in objects if o.type=='MESH'],clips=0,expansion=TAG))
    recipes['buildings'][name]={k:v for k,v in info.items() if k not in ['root','collection']}

# Studio previews use flat copies, so geometry remains easy to compare with no hidden animation state.
tree=ast.parse((ROOT/'Tools/build_air_city_expansion.py').read_text(encoding='utf-8'))
names={'tagged','studio','label','display','fit_camera'}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[]),'<studio helpers>','exec'))
previews=[]
s,c=studio('19_Glazing_Review',(0,0,1),16,(2200,1500))
for row,(kind,meta) in enumerate([('Coupe',vehicle_meta),('JetBike',vehicle_meta),('Warden_Survey',airmeta)]):
    info=meta['types'][kind]
    display(info['default_parts'],c,(0,(row-1)*6,0))
    label(c,kind.upper()+' / FRAMED LAMINATED PANES',(0,(row-1)*6-2.6,-.18),.28)
previews.append((s,'24_glazing_review'))
s,c=studio('20_Architecture_Styles',(0,0,18),150,(2800,1800))
offsets=[(-44,12,0),(-17,15,0),(14,12,0),(44,12,0),(25,-21,0)]
for (name,info),offset in zip(BUILDINGS.items(),offsets):
    for obj in info['collection'].objects:
        if obj.type!='MESH':continue
        ob=tagged(obj.copy());ob.asset_clear();ob.parent=None;ob.location=Vector(obj.location)+Vector(offset);c.objects.link(ob)
    label(c,name.replace('_',' '),(offset[0],offset[1]-12,-.18),.72)
    label(c,str(info['floor_count'])+' FLOORS',(offset[0],offset[1]-14,-.18),.64)
previews.append((s,'25_architecture_styles'))
s,c=studio('23_Architecture_Closeups',(0,0,2),17,(2000,1400))
for i in range(3):
    display(['Arch_Facade_Frame_3m','Arch_Facade_Glazing_3m','Arch_Facade_Porcelain_Fin'],c,((i-1)*3,3,0))
    display(['Arch_Balcony_3m','Arch_Planter_3m'],c,((i-1)*3,1,3.6))
for i in range(6):display([f'Arch_Vault_Glazing_{i}',f'Arch_Vault_Rib_{i}'],c,(0,-4,0))
label(c,'RECESSED PANES / SEALS / SUN FINS / TERRACE / VAULT',(0,-7,-.18),.28)
previews.append((s,'26_architecture_closeups'))
s,c=studio('21_Crosswalk_Separation',(0,0,.1),26,(2000,1300))
transit=json.loads((ROOT/'Documentation/transit_modules.json').read_text(encoding='utf-8'))
mods=transit.get('modules',transit)
for x,mod in [(-8,'Transit_CrosswalkPaint_2L'),(0,'Transit_Road_2L_Straight_4m'),(8,'Transit_Walk_W4_Straight_2m')]:
    display(mods[mod]['parts'],c,(x,0,0));label(c,mod.replace('Transit_',''),(x,-5,-.18),.28)
display(expanded(['Road_Crossing_2m']),c,(0,7,0))
label(c,'PAINT ONLY                    ROAD SUBSTRATE                    INDEPENDENT WALKWAY',(0,-7,-.18),.30)
previews.append((s,'27_crosswalk_separation'))
s,c=studio('22_Legacy_Asset_Refinements',(0,0,1),15,(2200,1600))
for row,name in enumerate(before_ids):
    ob=tagged(bpy.data.objects.new('Refinement_Before_'+name,before[name]));c.objects.link(ob)
    lo=min(v.co.z for v in ob.data.vertices);ob.location=(-3.5,(1-row)*4,-lo)
    display(expanded([name]),c,(3.5,(1-row)*4,-lo))
label(c,'PREVIOUS SOLID GLAZING',(-3.5,7,-.18),.30)
label(c,'FRAMES / SEALS / INDIVIDUAL PANES',(3.5,7,-.18),.30)
previews.append((s,'28_legacy_refinements'))
for s,file in previews:fit_camera(s)

catalog['assets']=[a for a in catalog['assets'] if a['id'] not in changed and a.get('expansion')!=TAG]+ASSETS
catalog['exports']=exports
write('Documentation/asset_catalog.json',catalog)
write('Documentation/vehicle_assemblies.json',vehicle_meta)
write('Documentation/aircraft_assemblies.json',airmeta)
write('Documentation/assembly_recipes.json',recipes)
write('Documentation/architecture_styles.json',dict(schema='erma_architecture_styles_v1',units='meters',styles={n:{k:v for k,v in i.items() if k not in ['root','collection']} for n,i in BUILDINGS.items()}))
write('Validation/refinement_pipeline_report.json',dict(schema='detail_first_lossless_cleanup_v1',summary=summary,parts=PIPE.records,glazing_companions=COMPANIONS,source_scene=rawscene.name))
# Existing air report remains an accurate description of the same original 219 part IDs.
report=json.loads((ROOT/'Validation/detail_pipeline_report.json').read_text(encoding='utf-8'))
records={r['id']:r for r in PIPE.records}
report['parts']=[records.get(r['id'],r) for r in report['parts']]
for key in ['source_triangles','optimized_triangles','removed_triangles']:
    report['summary'][key]=sum(r[key] for r in report['parts'])
report['summary']['max_triangles']=max(r['optimized_triangles'] for r in report['parts'])
write('Validation/detail_pipeline_report.json',report)
report=json.loads((ROOT/'Validation/source_report.json').read_text(encoding='utf-8'))
report.update(mesh_parts=len(catalog['assets']),max_triangles=max(a['triangles'] for a in catalog['assets']),all_parts_under_500=True)
write('Validation/source_report.json',report)
bpy.context.window.scene=bpy.data.scenes[entry_scene]
bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE),compress=True)
print('REFINEMENT_SAVED',len(ASSETS),'PARTS',len(BUILDINGS),'BUILDINGS',flush=True)
if '--no-render' not in sys.argv:
    for s,file in previews:
        bpy.context.window.scene=s;s.render.filepath=str(ROOT/'Preview'/f'{file}.png')
        bpy.ops.render.render(write_still=True);print('PREVIEW_SAVED',file,flush=True)
print('REFINEMENT_COMPLETE',flush=True)
