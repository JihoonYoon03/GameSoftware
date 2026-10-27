"""Render unchanged assemblies before/after the no-split budget migration."""
import ast,bpy,json,math,sys,zipfile
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from verify_resource_pack import read_glb,accessor
cat=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
air=json.loads((ROOT/'Documentation/aircraft_assemblies.json').read_text(encoding='utf-8'))
assets={a['id']:a for a in cat['assets']};parts={e['mesh_parts'][0]:e['file'] for e in cat['exports'] if e['type']=='module' and len(e['mesh_parts'])==1}
MATS={m.name:m for m in bpy.data.materials};z=zipfile.ZipFile(ROOT/'Source/Archives/Pre200Delivery_20261026.zip')
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ['studio','ground','text_label']],type_ignores=[]),'studio_helpers','exec'),globals())
class BytesFile:
    def __init__(self,data):self.data=data
    def read_bytes(self):return self.data
def oldmesh(pid):
    d,b=read_glb(BytesFile(z.read(parts[pid].replace('\\','/'))));pp=[];ff=[];mm=[];uv=[];normals=[];materials=[]
    for p in d['meshes'][0]['primitives']:
        offset=len(pp);aa=p['attributes'];ps=accessor(d,b,aa['POSITION']);pp.extend((x,-zz,y) for x,y,zz in ps)
        uv.extend(accessor(d,b,aa['TEXCOORD_0']));normals.extend((x,-zz,y) for x,y,zz in accessor(d,b,aa['NORMAL']))
        ids=[i[0]+offset for i in accessor(d,b,p['indices'])];ff.extend(tuple(ids[i:i+3]) for i in range(0,len(ids),3));mm.extend([len(materials)]*(len(ids)//3))
        materials.append(MATS[d['materials'][p['material']]['name']])
    mesh=bpy.data.meshes.new('ArchivedComparison');mesh.from_pydata(pp,[],ff);mesh.update()
    for m in materials:mesh.materials.append(m)
    layer=mesh.uv_layers.new()
    for p,m in zip(mesh.polygons,mm):p.material_index=m;p.use_smooth=True
    for loop in mesh.loops:layer.data[loop.index].uv=uv[loop.vertex_index]
    mesh.normals_split_custom_set([normals[l.vertex_index] for l in mesh.loops]);return mesh
cases=[('armor',recipes['NPC_HeavyCombat']['parts'],recipes['NPC_HeavyCombat'].get('offsets',{}),False),
       ('face_hand',['Head_Angular','Face_Stern','Body_Hand_L'],{},False),
       ('lift_field',['Aircraft_Warden_Survey_LiftField_Front_L'],{},True),
       ('glazing',[n for n in assets if n.startswith('Vehicle_Sedan') and any(k in n for k in ['Cabin','Glazing','Window'])],{},False)]
# Vehicle IDs vary by kit revision; the glazing case always uses the real parts.
if not cases[-1][1]:
    candidates=[n for n in assets if assets[n]['family'].startswith('vehicle') and 'Glazing' in n]
    seed=candidates[0];prefix=seed.split('_Glazing')[0]
    cases[-1]=('glazing',[n for n in candidates if n.startswith(prefix)],{},False)
out=ROOT/'Preview/PolygonBudget';out.mkdir(exist_ok=True)
for name,pids,offsets,top in cases:
    obs=[];coords=[]
    for pid in pids:
        source=bpy.data.objects[assets[pid]['blender_object']];coords.extend(v.co+Vector(offsets.get(pid,(0,0,0))) for v in source.data.vertices)
    lo=Vector(tuple(min(p[k] for p in coords) for k in range(3)));hi=Vector(tuple(max(p[k] for p in coords) for k in range(3)));center=(lo+hi)*.5
    span=max(hi.x-lo.x,hi.y-lo.y,hi.z-lo.z);distance=span*.75;target=Vector((0,0,span*.55))
    s,c=studio('Budget_'+name,(0,-span*3,span*(3 if top else 1.7)),target,span*3.4,(1500,900));s.cycles.samples=16
    ground(c,span*5,-.025);bpy.context.window.scene=s
    for variant,x in [('BEFORE',-distance),('AFTER',distance)]:
        for pid in pids:
            mesh=oldmesh(pid) if variant=='BEFORE' else bpy.data.objects[assets[pid]['blender_object']].data.copy()
            ob=bpy.data.objects.new(variant+'_'+pid,mesh);c.objects.link(ob)
            ob.location=Vector((x,0,span*.55))-center+Vector(offsets.get(pid,(0,0,0)))
            if name=='armor':
                for i,m in enumerate(mesh.materials):
                    if m.name.startswith('Skin_'):mesh.materials[i]=MATS['Skin_Deep']
        text_label(c,variant,(x,-span*.7,-.02),span*.095)
    s.render.filepath=str(out/(name+'.png'));bpy.ops.render.render(write_still=True)
    print('BUDGET_COMPARISON_RENDERED',name,pids,flush=True)
