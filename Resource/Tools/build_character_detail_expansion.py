"""Detail-first replacement of every HUMAN_7H module, preserving other expansions.

Author anatomical contours, facial features, seams, closures and equipment detail;
retain editable sources, then run the common surface-preserving cleanup.
"""
import ast
import json
import math
import sys
import hashlib
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector,Matrix,Quaternion

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'Source/Erma_Resource_Library.blend'
TAG='character_detail_v3'
REVISION='human_detail_first_v3'
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
entry_scene=bpy.context.scene.name
bpy.context.preferences.filepaths.save_version=0
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
oldassets={a['id']:a for a in catalog['assets']}
original_ids={a['id'] for a in catalog['assets'] if a.get('skeleton')=='HUMAN_7H' and not a.get('human_detail_companion')}
assert len(original_ids)==785,len(original_ids)
def protected_signature():
    digest=hashlib.sha256()
    for a in sorted(catalog['assets'],key=lambda a:a['id']):
        if a.get('skeleton')=='HUMAN_7H':continue
        o=bpy.data.objects[a['id']]
        digest.update(a['id'].encode())
        digest.update(json.dumps([[tuple(v.co) for v in o.data.vertices],
                                  [tuple(p.vertices) for p in o.data.polygons],
                                  [m.name for m in o.data.materials]],separators=(',',':')).encode())
    return digest.hexdigest()
protected_before=protected_signature()
comparison_characters=['Player_Explorer','Ria_Engineer','NPC_HeavyCombat','NPC_Civilian']
comparison_ids=set(n for k in comparison_characters for n in recipes[k]['parts'] if n in original_ids)
comparison_ids.update(['Head_Oval','Face_Neutral','Body_Hand_L','Boot_L','Outfit_Engineer_Female_Normal_Torso'])
before={n:(bpy.data.objects.get('Character_Before__'+n) or bpy.data.objects[n]).data.copy() for n in comparison_ids}
for s in list(bpy.data.scenes):
    if s.get('expansion')==TAG:bpy.data.scenes.remove(s)
for o in list(bpy.data.objects):
    if o.get('expansion')==TAG and o.name not in original_ids:bpy.data.objects.remove(o,do_unlink=True)
for c in list(bpy.data.collections):
    if c.get('expansion')==TAG:bpy.data.collections.remove(c)
scene=bpy.data.scenes['00_Library'];bpy.context.window.scene=scene;scene.frame_set(0)
human=bpy.data.objects['HUMAN_7H']
MATS={m.name:m for m in bpy.data.materials}
oldobjects={n:bpy.data.objects[n] for n in original_ids}
for n,o in oldobjects.items():o.name='PreviousCharacter__'+n
LIB=bpy.data.collections['RESOURCE_LIBRARY']

def collection(name):
    c=bpy.data.collections.get(name)
    if c:return c
    c=bpy.data.collections.new(name);c['expansion']=TAG;LIB.children.link(c)
    return c

def material(name,color,rough=.6,metal=0):
    m=MATS.get(name) or bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1)
    p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
    MATS[name]=m

material('Fabric_Seam',(.055,.075,.085),.88)
material('Fastener_Titanium',(.39,.47,.49),.32,.8)
material('Visor_Smoked',(.035,.15,.20),.16,.45)
material('Hair_Silver',(.20,.24,.25),.82)
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
def extract(tree,names):
    exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,(ast.FunctionDef,ast.ClassDef)) and n.name in names],type_ignores=[]),'<authoring helpers>','exec'),globals())
extract(tree,{'Geo','human_modules','clone_actor'})
LegacyGeo=Geo
sys.path.insert(0,str(ROOT/'Tools'))
from detail_mesh_pipeline import DetailGeometry,DetailPipeline
rawscene=bpy.data.scenes.new('25_Character_Detail_Sources');rawscene['expansion']=TAG
rawcol=bpy.data.collections.new('Character_Editable_Sources');rawcol['expansion']=TAG
rawscene.collection.children.link(rawcol)
archive=bpy.data.collections.new('Character_Previous_Comparison');archive['expansion']=TAG
rawscene.collection.children.link(archive)
for name,mesh in before.items():
    ob=bpy.data.objects.new('Character_Before__'+name,mesh);ob['expansion']=TAG;ob['role']='previous_character_comparison'
    archive.objects.link(ob)
PIPE=DetailPipeline(rawcol,MATS,TAG,allow_draft_over_budget=True)
ASSETS=[];PROTOTYPES={};COMPANIONS={}
HC=bpy.data.collections.get('01a_Human_Reference_v1') or collection('01a_Human_Reference_v1')
H=1.8;HH=H/7;NECK=H-HH

def beam(g,a,b,width,depth,mat,bone):
    a,b=Vector(a),Vector(b);d=b-a
    return g.box((a+b)/2,(width,depth,d.length),mat,bone,d.to_track_quat('Z','Y').to_matrix())

def subset(g,components):
    h=Geo()
    for component in sorted(components):
        faces=[f for f,c in zip(g.f,g.component_ids) if c==component]
        indices=sorted({v for f in faces for v in f});remap={v:i for i,v in enumerate(indices)}
        h.add([g.v[v] for v in indices],[tuple(remap[v] for v in f) for f in faces],
              g.component_materials[component],g.component_bones[component])
    return h

def emit_part(g,name,col,rig,slot,family,companion=False):
    obj,a=PIPE.build(g,name,col,rig,slot,family)
    a={**oldassets.get(name,{}),**a,'detail_revision':REVISION}
    obj['detail_revision']=REVISION
    if companion:
        a['expansion']=TAG;a['human_detail_companion']=True;obj['expansion']=TAG
    for key in ['body_profile','purpose','legacy_reference','attachment_socket']:
        if key in oldobjects.get(name,{}):obj[key]=oldobjects[name][key]
    ASSETS.append(a);PROTOTYPES[name]=obj
    return obj

class Geo(DetailGeometry,LegacyGeo):
    def box(self,c,s,mat,bone=None,rot=None):
        # Human-scale shells and equipment need manufactured edges below vehicle scale.
        hard=mat in {'Steel','Alloy_Light','Ceramic','Copper','Fastener_Titanium','Warning'}
        if not hard or min(s)<.028 or max(s)<.09:return LegacyGeo.box(self,c,s,mat,bone,rot)
        bm=bmesh.new();bmesh.ops.create_cube(bm,size=1)
        for v in bm.verts:v.co=Vector((v.co.x*s[0],v.co.y*s[1],v.co.z*s[2]))
        bmesh.ops.bevel(bm,geom=list(bm.edges),offset=min(min(s)*.12,.007),segments=1,affect='EDGES',clamp_overlap=True)
        bm.verts.ensure_lookup_table();bm.verts.index_update()
        self.add([(rot@v.co if rot else v.co)+Vector(c) for v in bm.verts],
                 [tuple(v.index for v in f.verts) for f in bm.faces],mat,bone)
        bm.free();return self

    def rings(self,centers,radii,mat,bone=None,sides=8,caps=True,axis=None):
        centers=[Vector(p) for p in centers]
        radii=[tuple(r) if isinstance(r,(tuple,list)) else (r,r) for r in radii]
        radius=max(max(r) for r in radii)
        sides=max(sides,12 if radius>.06 else 8)
        # Intermediate sections follow a smooth bounded profile. Joint end rings stay exact.
        soft=mat.startswith(('Skin_','Cloth_')) or mat=='Rubber'
        if soft and len(centers)>=3 and (centers[-1]-centers[0]).length>.19:
            cc=[];rr=[]
            for i in range(len(centers)-1):
                cc.append(centers[i]);rr.append(radii[i])
                cc.append(centers[i].lerp(centers[i+1],.5))
                prev=radii[max(0,i-1)];nxt=radii[min(len(radii)-1,i+2)]
                mid=[]
                for k in range(2):
                    a,b=radii[i][k],radii[i+1][k]
                    value=(a+b)/2+(a-prev[k]-nxt[k]+b)/16
                    mid.append(max(min(a,b),min(max(a,b),value)))
                rr.append(tuple(mid))
            cc.append(centers[-1]);rr.append(radii[-1]);centers,radii=cc,rr
        return LegacyGeo.rings(self,centers,radii,mat,bone,sides,caps,axis)

    def ellipsoid(self,c,s,mat,bone=None,sides=10,rings=6):
        # More readable curved shoulders/knuckles, without oversampling small seam details.
        if max(s)>.045:sides=max(sides,10);rings=max(rings,4)
        return LegacyGeo.ellipsoid(self,c,s,mat,bone,sides,rings)

    def make(self,name,col,rig=None,slot='',family=''):
        g=author_detail(self,name,slot)
        if name.startswith('Outfit_') and name.endswith('_Torso'):
            base={i for i,(mat,bone) in enumerate(zip(g.component_materials,g.component_bones))
                  if i==0 or mat in {'Fabric_Seam'} or (mat.startswith('Cloth_') and bone.startswith('thigh'))}
            # Textile/coattails and detachable external harness/armor are distinct service units.
            outer=set(range(len(g.component_bones)))-base
            if outer:
                companion=name+'_OuterAssembly'
                emit_part(subset(g,outer),companion,col,rig,'outfit.outer_assembly',family,True)
                COMPANIONS[name]=companion
                g=subset(g,base)
        return emit_part(g,name,col,rig,slot,family)

def head(name):
    rx,ry,skin={'Oval':(.100,.090,'Skin_Warm'),'Angular':(.108,.090,'Skin_Deep'),
                'Round':(.112,.095,'Skin_Light'),'Narrow':(.091,.084,'Skin_Cool')}[name.removeprefix('Head_')]
    levels=[(0,.052,.055),(.025,rx*.73,ry*.77),(.060,rx*.90,ry*.94),(.105,rx,ry),
            (.145,rx*.97,ry*.94),(.188,rx*.96,ry*.98),(.225,rx*.79,ry*.84),(H-NECK,.030,.035)]
    g=Geo()
    # Jaw, cheek and brow transitions are explicit profiles, not an enlarged ellipsoid.
    LegacyGeo.rings(g,[(0,.002 if z<.07 else 0,NECK+z) for z,w,d in levels],[(w,d) for z,w,d in levels],skin,'head',12)
    # Four shared faces need a compatible facial plane. Shape the cheeks/brow
    # around the eye layer so the eyes and eyebrows are seated, not floating.
    planes={.060:.084,.105:.088,.145:.0885,.188:.088}
    seated=[]
    for x,y,z in g.v:
        level=round(z-NECK,3)
        if level in planes and y<0 and abs(x)<rx*.80:y=-max(abs(y),planes[level])
        seated.append((x,y,z))
    g.v=seated
    for s in [-1,1]:
        g.ellipsoid((s*(rx+.005),.005,NECK+.115),(.018,.025,.031),skin,'head',8,3)
        g.box((s*(rx+.014),-.012,NECK+.118),(.007,.006,.019),'Tissue','head')
    return g

def face(name):
    g=Geo();alert=name.endswith('Alert');stern=name.endswith('Stern');scale=1.10 if alert else .85 if stern else 1
    for s in [-1,1]:
        x=s*.039;z=NECK+.139;yy=-.091
        g.ellipsoid((x,yy,z),(.017*scale,.006,.010*scale),'Eye_White','head',8,3)
        g.rod((x,-.096,z),(x,-.100,z),.006,.006,'Eye_Dark','head',8)
        g.box((x-.002,-.103,z+.003),(.003,.001,.003),'Eye_White','head')
        for sign in [-1,1]:
            points=[(x-.017*scale,-.095,z),(x,-.097,z+sign*.010*scale),(x+.017*scale,-.095,z)]
            for a,b in zip(points,points[1:]):beam(g,a,b,.003,.003,'Skin_Warm','head')
        a=(x-s*.017,-.090,NECK+.161+( .004 if alert else 0))
        b=(x+s*.018,-.087,NECK+.159+(-.003 if stern else .003))
        beam(g,a,b,.006,.006,'Hair_Charcoal','head')
    g.rod((0,-.084,NECK+.134),(0,-.113,NECK+.101),.011,.008,'Skin_Warm','head',6)
    for s in [-1,1]:g.box((s*.006,-.111,NECK+.098),(.005,.004,.003),'Tissue','head')
    beam(g,(-.023,-.085,NECK+.059),(0,-.092,NECK+.057),.004,.004,'Tissue','head')
    beam(g,(0,-.092,NECK+.057),(.023,-.085,NECK+.059),.004,.004,'Tissue','head')
    return g

def hand(suf):
    s=1 if suf=='L' else -1;bone='hand.'+suf
    g=Geo().rings([(s*.447,-.020,.913),(s*.454,-.021,.885),(s*.458,-.022,.859)],[(.028,.026),(.034,.027),(.029,.023)],'Skin_Warm',bone,10)
    # Four shaped digits and a separate thumb; existing hand bone controls the assembled hand.
    for i,offset in enumerate([-.021,-.007,.007,.021]):
        x=s*.458+offset;length=[.038,.050,.047,.036][i]
        g.rings([(x,-.026,.862),(x+s*.004,-.032,.842),(x+s*.004,-.041,.862-length)],
                [.008,.008,.006],'Skin_Warm',bone,6)
        g.box((x+s*.004,-.045,.872-length),(.008,.002,.009),'Skin_Light',bone)
    g.rings([(s*.433,-.032,.897),(s*.416,-.050,.878),(s*.425,-.059,.857)],[.013,.011,.008],'Skin_Warm',bone,6)
    return g

def boot(suf):
    s=1 if suf=='L' else -1;bone='foot.'+suf;x=s*.115
    g=Geo().box((x,-.065,.018),(.12,.26,.036),'Rubber',bone)
    g.box((x,-.055,.082),(.108,.235,.10),'Rubber',bone)
    g.box((x,-.143,.082),(.11,.070,.064),'Ceramic',bone)
    g.box((x,.045,.075),(.112,.035,.080),'Steel',bone)
    for y in [-.16,-.095,-.03,.035]:
        for side in [-1,1]:g.box((x+side*.058,y,.023),(.012,.032,.020),'Steel',bone)
    for i in range(3):g.box((x,-.090+i*.027,.135),(.070,.009,.008),'Fastener_Titanium',bone)
    return g

def author_detail(g,name,slot):
    if name.startswith('Head_'):return head(name)
    if name.startswith('Face_'):return face(name)
    if name.startswith('Body_Hand_'):return hand(name[-1])
    if name.startswith('Boot_'):return boot(name[-1])
    if name.startswith('Outfit_'):
        role=name.split('_')[1];bone=g.w[0];pts=[Vector(v) for v in g.v]
        lo=[min(v[k] for v in pts) for k in range(3)];hi=[max(v[k] for v in pts) for k in range(3)]
        x=(lo[0]+hi[0])/2;y=lo[1]-.003;z=(lo[2]+hi[2])/2
        if name.endswith('_Torso'):
            profile=next((p for p in BODY_PROFILES if '_'+p+'_' in name),None) if 'BODY_PROFILES' in globals() else None
            p=BODY_PROFILES[profile] if profile else dict(waist=.14,rib=.20,front=.125,belly=.11,back=.10,bust=0)
            for side in [-1,1]:
                beam(g,(side*p['rib']*.70,-p['front']-.025,1.40),(side*p['waist']*.78,-p['belly']-.024,1.10),.006,.004,'Fabric_Seam','chest')
            g.box((0,-p['front']-.036,1.31),(.008,.007,.21),'Fastener_Titanium','chest')
            for zz in [1.16,1.28,1.40]:g.box((-.025,-p['front']-.044,zz),(.012,.008,.014),'Fastener_Titanium','chest')
            g.box((.055,-p['front']-.045,1.39),(.045,.008,.014),'Glow_Cyan','chest')
        else:
            # Long seams and small articulated overlap strips keep joint ends clear.
            length=hi[2]-lo[2]
            beam(g,(x,y,lo[2]+length*.22),(x,y,hi[2]-length*.22),.005,.003,'Fabric_Seam',bone)
            for side in [-1,1]:g.box((x+side*(hi[0]-lo[0])*.21,y-.003,z),(.009,.006,.020),'Fastener_Titanium',bone)
            if role in ['Guard','Miner','HeavyCombat']:
                for dz in [-.026,.026]:g.box((x,y-.006,z+dz),((hi[0]-lo[0])*.40,.008,.009),'Alloy_Light',bone)
    elif name.startswith(('Accessory_','Gear_','Hat_','Helmet_','Weapon_')):
        pts=[Vector(v) for v in g.v];lo=[min(v[k] for v in pts) for k in range(3)];hi=[max(v[k] for v in pts) for k in range(3)]
        cx,cy,cz=[(a+b)/2 for a,b in zip(lo,hi)];bone=g.w[0]
        outer=hi[1]+.004 if 'back' in bone else lo[1]-.004
        width=min(hi[0]-lo[0],.20);height=min(hi[2]-lo[2],.20)
        if 'Helmet' in name or 'Hat_' in name or name in ['Accessory_Goggles','Accessory_Research_Visor']:
            for side in [-1,1]:
                g.box((side*.101,lo[1]-.004,1.70),(.014,.014,.056),'Alloy_Light',bone)
            for zz in [1.674,1.742]:g.box((0,lo[1]-.005,zz),(.18,.014,.010),'Rubber',bone)
            for x in [-.027,0,.027]:g.box((x,lo[1]-.009,1.635),(.013,.009,.018),'Fastener_Titanium',bone)
        else:
            # A recessed service cover, latch and short status strip, sized to the actual device.
            g.box((cx,outer,cz),(width*.72,.009,height*.60),'Rubber',bone)
            for dx in [-.34,.34]:
                for dz in [-.27,.27]:g.box((cx+dx*width,outer+(-.006 if outer<cy else .006),cz+dz*height),(.009,.006,.009),'Fastener_Titanium',bone)
            g.box((cx,outer+(-.008 if outer<cy else .008),cz+height*.15),(width*.50,.006,.010),'Glow_Cyan',bone)
    elif name.startswith('Hair_') and 'HatClearance' not in name:
        # Carved locks follow the parting/crown rather than random surface subdivisions.
        # Use existing scalp edge coordinates to avoid detached straight spikes.
        scalp_faces=[f for f,c in zip(g.f,g.component_ids) if c==0]
        scalp_vertices=sorted({i for f in scalp_faces for i in f})
        ring_size=len(scalp_vertices)//3
        for side in [0,1]:
            index=round(ring_size*.67)+side
            a=Vector(g.v[ring_size+index])+Vector((0,-.001,.0005))
            b=Vector(g.v[2*ring_size+index])+Vector((0,-.001,.0005))
            beam(g,a,b,.003,.002,'Hair_Charcoal','head')
    return g

human_modules()
v2tree=ast.parse((ROOT/'Tools/human_design_v2.py').read_text(encoding='utf-8'))
# Run only the authored geometry definitions: skip reference renaming, boot vertex edits,
# socket recreation and documentation writes owned by the original builder.
nodes=[]
for node in v2tree.body:
    if node.lineno>=339:continue
    if isinstance(node,(ast.Import,ast.ImportFrom)):continue
    if isinstance(node,ast.Assign):
        targets=[t.id for t in node.targets if isinstance(t,ast.Name)]
        if targets==['HC'] or any(isinstance(t,ast.Attribute) for t in node.targets):continue
    if isinstance(node,ast.For) and node.lineno in [7,101,229]:continue
    if isinstance(node,ast.Expr):continue
    nodes.append(node)
# Standalone geometry construction calls and accessory registrations are expressions too.
for node in v2tree.body:
    if 242<=node.lineno<328 and isinstance(node,ast.Expr):nodes.append(node)
nodes.sort(key=lambda n:n.lineno)
exec(compile(ast.Module(body=nodes,type_ignores=[]),'<human v2 geometry>','exec'),globals())
assert original_ids<=set(PROTOTYPES),(len(PROTOTYPES),sorted(original_ids-set(PROTOTYPES)))

# Apply the original cloth blending to both editable sources and final textile/outer modules.
for row in PIPE.records:
    PROTOTYPES[row['id']]['revision']=3
    if row['id'].startswith('Outfit_') and ('_Torso' in row['id']):
        for obj in [PROTOTYPES[row['id']],bpy.data.objects[row['source_object']]]:torso_weights(obj)
for asset in ASSETS:asset['revision']=3

summary=dict(original_parts_replaced=len(original_ids),parts=len(PIPE.records),companion_modules=len(COMPANIONS),
             source_triangles=sum(r['source_triangles'] for r in PIPE.records),
             optimized_triangles=sum(r['optimized_triangles'] for r in PIPE.records),
             removed_triangles=sum(r['removed_triangles'] for r in PIPE.records),
             max_triangles=max(r['optimized_triangles'] for r in PIPE.records),
             max_surface_error_m=max(r['max_surface_error_m'] for r in PIPE.records),
             over_budget=[(r['id'],r['optimized_triangles']) for r in PIPE.records if r['optimized_triangles']>500])
draft=ROOT/'Validation/character_detail_draft.json';draft.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print('CHARACTER_DETAIL_GEOMETRY',json.dumps(summary),flush=True)
if summary['over_budget']:raise RuntimeError('Revise functional modules: '+str(summary['over_budget']))
if '--geometry-check' in sys.argv:raise SystemExit(0)

def copy_weights(source,target):
    target.vertex_groups.clear()
    groups={g.name:target.vertex_groups.new(name=g.name) for g in source.vertex_groups}
    for v in source.data.vertices:
        for link in v.groups:groups[source.vertex_groups[link.group].name].add([v.index],link.weight,'REPLACE')

def match_palette(obj,mapping):
    for i,m in enumerate(obj.data.materials):
        if m.name in mapping:obj.data.materials[i]=mapping[m.name]

# Refresh all saved actors and review scenes, including copies with a different skin palette.
for name,old in oldobjects.items():
    src=PROTOTYPES[name]
    instances=[o for o in bpy.data.objects if o.type=='MESH' and o.get('asset_id')==name and o!=src and o!=old]
    for instance in instances:
        palette={a.name:b for a,b in zip(old.data.materials,instance.data.materials)}
        instance.data=src.data.copy();copy_weights(src,instance);match_palette(instance,palette)
        instance['detail_revision']=REVISION;instance['detail_source']=src['detail_source'];instance['triangle_count']=src['triangle_count']
        if name in COMPANIONS:
            outer=PROTOTYPES[COMPANIONS[name]];ob=outer.copy();ob.data=outer.data.copy();ob.asset_clear()
            ob.name=instance.name+'__OuterAssembly';ob['expansion']=TAG;ob.parent=instance.parent;ob.matrix_basis=instance.matrix_basis.copy()
            for m in ob.modifiers:
                if m.type=='ARMATURE':m.object=next((x.object for x in instance.modifiers if x.type=='ARMATURE'),None)
            match_palette(ob,palette)
            for c in instance.users_collection:c.objects.link(ob)
    bpy.data.objects.remove(old,do_unlink=True)

def expanded(parts):
    out=[]
    for n in parts:
        if n not in out:out.append(n)
        if n in COMPANIONS and COMPANIONS[n] not in out:out.append(COMPANIONS[n])
    return out

for name,info in recipes.items():
    if isinstance(info,dict) and info.get('body'):
        info['parts']=expanded(info['parts']);info['detail_revision']=REVISION

def reset(rig):
    for b in rig.pose.bones:b.location=(0,0,0);b.rotation_mode='QUATERNION';b.rotation_quaternion=(1,0,0,0);b.scale=(1,1,1)

def export(file,objects,rig,animated=False):
    if '--preview-only' in sys.argv:return
    bpy.context.window.scene=scene
    for o in scene.objects:o.select_set(False)
    for o in objects:o.select_set(True)
    rig.select_set(True);bpy.context.view_layer.objects.active=rig
    rig.animation_data.action=None;rig.animation_data.use_nla=True
    for t in rig.animation_data.nla_tracks:t.mute=not animated
    reset(rig);scene.frame_set(0)
    p=ROOT/file;p.parent.mkdir(parents=True,exist_ok=True);staged=p.with_name(p.stem+'.staging.glb')
    bpy.ops.export_scene.gltf(filepath=str(staged),export_format='GLB',use_selection=True,use_active_scene=True,
        export_animations=animated,export_animation_mode='NLA_TRACKS',export_nla_strips=True,
        export_force_sampling=True,export_frame_range=False,export_skins=True,export_def_bones=False,
        export_yup=True,export_extras=True,export_cameras=False,export_lights=False,
        export_materials='EXPORT',export_apply=False,export_texcoords=True,export_normals=True,export_anim_slide_to_zero=True)
    staged.replace(p)
    for t in rig.animation_data.nla_tracks:t.mute=True
    reset(rig)

exports=[]
selective='--anatomy-exports-only' in sys.argv
if selective:assert all(oldassets[n].get('detail_revision')==REVISION for n in original_ids),'partial refresh requires a complete v3 pack'
for e in catalog['exports']:
    if e.get('expansion')==TAG:continue
    e={**e,'file':e['file'].replace('\\','/')}
    if e['type']=='character':
        name=Path(e['file']).stem;info=recipes[name]
        temp=collection('Character_Export_Temporary')
        rig,objects=clone_actor(name,human,info['parts'],temp,skin=info.get('skin'),part_offsets=info.get('offsets'))
        for original in human.animation_data.nla_tracks:
            t=rig.animation_data.nla_tracks.new();t.name=original.name
            strip=t.strips.new(original.name,0,original.strips[0].action);strip.name=original.name;t.mute=True
        export(e['file'],objects,rig,True);e['mesh_parts']=info['parts']
        for o in objects+[rig]:bpy.data.objects.remove(o,do_unlink=True)
        bpy.data.collections.remove(temp)
    elif any(n in original_ids for n in e['mesh_parts']):
        if not selective or any(n.startswith(('Head_','Body_Hand_','Hair_')) for n in e['mesh_parts']):
            export(e['file'],[PROTOTYPES[n] for n in e['mesh_parts']],human)
    exports.append(e)
for name in COMPANIONS.values():
    file=f'Export/Modules/{name}.glb';export(file,[PROTOTYPES[name]],human)
    exports.append(dict(file=file,type='module',mesh_parts=[name],clips=0,expansion=TAG))

# Dedicated before/after and close-up scenes use the updated canonical geometry.
tree=ast.parse((ROOT/'Tools/build_air_city_expansion.py').read_text(encoding='utf-8'))
extract(tree,{'tagged','studio','label','fit_camera'})
previews=[]
def display_mesh(mesh,c,offset,name):
    ob=bpy.data.objects.new(name,mesh);ob['expansion']=TAG;c.objects.link(ob);ob.location=offset
    return ob

s,c=studio('26_Character_Detail_Comparison',(0,0,.8),12,(2400,1700))
for i,name in enumerate(comparison_characters):
    info=recipes[name];x=(i-1.5)*2.2
    for part in [n for n in info['parts'] if n in before]:
        pos=Vector((x,3.1,0))+Vector(info.get('offsets',{}).get(part,(0,0,0)))
        ob=display_mesh(before[part],c,pos,'Before_'+part)
    for part in info['parts']:
        pos=Vector((x,0,0))+Vector(info.get('offsets',{}).get(part,(0,0,0)))
        display_mesh(PROTOTYPES[part].data,c,pos,'After_'+part)
    label(c,name.replace('_',' '),(x,-.65,-.18),.15)
label(c,'PREVIOUS / SIMPLE SURFACES',(0,4.5,-.18),.23)
label(c,'REFINED CONTOURS / SEAMS / HARDWARE / DETACHABLE CHEST ASSEMBLIES',(0,-1.5,-.18),.20)
previews.append((s,'29_character_detail_comparison'))
s,c=studio('27_Character_Detail_Closeups',(0,0,.2),7,(2400,1700))
for row,parts in enumerate([['Head_Oval','Face_Neutral','Hair_SidePart'],['Body_Hand_L'],['Boot_L'],['Outfit_Engineer_Female_Normal_Torso','Outfit_Engineer_Female_Normal_Torso_OuterAssembly']]):
    x=(row-1.5)*1.8
    points=[v.co for part in parts for v in PROTOTYPES[part].data.vertices]
    low=[min(v[k] for v in points) for k in range(3)];high=[max(v[k] for v in points) for k in range(3)]
    factor=.85/(high[2]-low[2]);center=Vector(((low[0]+high[0])/2,(low[1]+high[1])/2,low[2]))
    for part in parts:
        source=PROTOTYPES[part];lo=min(v.co.z for v in source.data.vertices)
        ob=display_mesh(source.data,c,Vector((x,0,.06))-center*factor,'Close_'+part);ob.scale=(factor,)*3
    label(c,['FACE / EYELIDS / NOSE','PALM / DIGITS / THUMB','TOE CAP / TREAD / LACES','TEXTILE / ZIP / OUTER ASSEMBLY'][row],(x,-.72,-.18),.085)
s.camera.location=(2.6,-9,4.2);s.camera.rotation_euler=(Vector((0,0,.45))-s.camera.location).to_track_quat('-Z','Y').to_euler()
previews.append((s,'30_character_detail_closeups'))
s,c=studio('28_Character_Head_Study',(0,0,.4),5,(2200,1300))
for i,(head_name,hair_name,expression) in enumerate([('Oval','SidePart','Neutral'),('Angular','Buzz','Stern'),('Round','Bun','Alert'),('Narrow','Ponytail','Neutral')]):
    x=(i-1.5)*1.25
    for part in ['Head_'+head_name,'Face_'+expression,'Hair_'+hair_name]:
        ob=display_mesh(PROTOTYPES[part].data,c,Vector((x,0,.05))-Vector((0,0,NECK))*2.8,'Portrait_'+part)
        ob.scale=(2.8,)*3
    label(c,head_name.upper()+' / '+expression.upper(),(x,-.55,-.18),.10)
s.camera.location=(1.5,-8,3.1);s.camera.rotation_euler=(Vector((0,0,.45))-s.camera.location).to_track_quat('-Z','Y').to_euler()
previews.append((s,'31_character_heads'))
for s,file in previews:fit_camera(s)

def write(path,data):
    if '--preview-only' in sys.argv:return
    p=ROOT/path;t=p.with_suffix(p.suffix+'.new')
    t.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');t.replace(p)

changed=set(PROTOTYPES)
catalog['assets']=[a for a in catalog['assets'] if a['id'] not in changed and a.get('expansion')!=TAG]+ASSETS
catalog['exports']=exports
write('Documentation/asset_catalog.json',catalog)
write('Documentation/assembly_recipes.json',recipes)
write('Documentation/character_detail_assemblies.json',dict(schema=REVISION,skeleton='HUMAN_7H',original_parts=785,
    body_profiles=list(BODY_PROFILES),purposes=list(ROLE_META),outer_assemblies=COMPANIONS,
    assembly_rule='choose an outfit torso plus its matching outer assembly; preserve shared bind coordinates'))
protected_after=protected_signature()
assert protected_before==protected_after,'non-human geometry changed'
write('Validation/character_detail_pipeline_report.json',dict(schema='detail_first_lossless_cleanup_v1',summary=summary,
    protected_nonhuman_geometry_sha256=protected_after,other_expansions_preserved=True,
    source_scene=rawscene.name,comparison_scene='26_Character_Detail_Comparison',parts=PIPE.records))
report=json.loads((ROOT/'Validation/source_report.json').read_text(encoding='utf-8'))
report.update(mesh_parts=len(catalog['assets']),max_triangles=max(a['triangles'] for a in catalog['assets']),all_parts_under_500=True)
write('Validation/source_report.json',report)
bpy.context.window.scene=bpy.data.scenes[entry_scene]
if '--preview-only' not in sys.argv:bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE),compress=True)
print('CHARACTER_DETAIL_SAVED',len(ASSETS),'PARTS',len(exports),'TOTAL EXPORTS',flush=True)
if '--no-render' not in sys.argv:
    jobs=previews if '--preview-only' in sys.argv else previews+[(bpy.data.scenes[n],f) for n,f in [('01_Characters','01_characters'),('07_Body_Profiles','06_body_profiles'),('08_Outfit_Purposes','07_outfit_purposes'),('08b_Outfit_Silhouette_Check','08_outfits_grayscale')]]
    for s,file in jobs:
        bpy.context.window.scene=s;s.cycles.device='CPU';s.cycles.samples=16
        s.render.filepath=str(ROOT/'Preview'/f'{file}.png');bpy.ops.render.render(write_still=True)
        print('CHARACTER_PREVIEW',file,flush=True)
print('CHARACTER_DETAIL_COMPLETE',flush=True)
