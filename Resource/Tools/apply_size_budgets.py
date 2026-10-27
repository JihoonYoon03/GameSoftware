"""Apply the reviewed functional plan and export one reusable geometry bank."""
import ast,bpy,json,sys,math,hashlib
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from part_budget_policy import REVISION,triangle_budget
plan=json.loads((ROOT/'Validation/size_budget_plan.json').read_text(encoding='utf-8'))
assert not plan['failures'],plan['failures']
assert all(len(p['groups'])==1 and p['groups'][0]['id']==n for n,p in plan['parts'].items()),'User forbids budget-driven part splits'
assert (ROOT/'Source/Archives/Pre200Delivery_20261026.zip').exists()
cat=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
assert len(cat['assets'])==2293,'migration must start from the pre-200 snapshot'
original={a['id']:a for a in cat['assets']};prototypes={n:bpy.data.objects[p['source_object']] for n,p in plan['parts'].items()}
original_objects=list(bpy.data.objects);entry=bpy.context.scene.name
bpy.context.preferences.filepaths.save_version=0

def skin_signature(ob):return tuple(g.name for g in ob.vertex_groups)
def copy_weights(ob,data):
    ob.vertex_groups.clear()
    names=sorted({n for ws in data['weights'] for n,w in ws});groups={n:ob.vertex_groups.new(name=n) for n in names}
    for i,ws in enumerate(data['weights']):
        for n,w in ws:groups[n].add([i],w,'REPLACE')

def copy_instance_weights(ob,data,old_binding):
    # A static library prototype can also be reused as a rigid animated leaf.
    # Preserve that instance's authored binding instead of clearing its skin.
    if not any(data['weights']) and any(m.type=='ARMATURE' for m in ob.modifiers):
        assert len(old_binding)==1 and old_binding[0],(ob.name,'non-rigid instance needs an explicit remap')
        copy_weights(ob,{**data,'weights':[old_binding[0]]*len(data['points'])})
    else:copy_weights(ob,data)

def new_mesh(ob,g):
    source=ob.data;source.calc_loop_triangles()
    materials=list(source.materials);mesh=bpy.data.meshes.new(g['id']+'_BudgetMesh')
    mesh.from_pydata(g['points'],[],g['triangles']);mesh.update()
    for mat in materials:mesh.materials.append(mat)
    for face,m in zip(mesh.polygons,g['materials']):face.material_index=m
    uv=mesh.uv_layers.new(name='UVMap');src_uv=source.uv_layers.active
    trees={};tlist=list(source.loop_triangles);custom_normals=[Vector((0,0,1)) for loop in mesh.loops] if source.has_custom_normals else None
    for m in set(g['materials']):
        ids=[i for i,t in enumerate(tlist) if source.polygons[t.polygon_index].material_index==m]
        trees[m]=(ids,BVHTree.FromPolygons([v.co for v in source.vertices],[tlist[i].vertices[:] for i in ids],all_triangles=True))
    for face in mesh.polygons:
        ids,tree=trees[face.material_index];hit=tree.find_nearest(face.center)
        target=tlist[ids[hit[2]]]
        if target.normal.dot(face.normal)<.2:
            candidates=[i for i in ids if tlist[i].normal.dot(face.normal)>.3]
            if candidates:target=min((tlist[i] for i in candidates),key=lambda t:(sum((source.vertices[v].co for v in t.vertices),Vector())/3-face.center).length_squared)
        a,b,c=[source.vertices[i].co for i in target.vertices]
        face.use_smooth=source.polygons[target.polygon_index].use_smooth
        ab,ac=b-a,c-a;d00=ab.dot(ab);d01=ab.dot(ac);d11=ac.dot(ac);den=d00*d11-d01*d01
        coords=[Vector(src_uv.data[i].uv) for i in target.loops] if src_uv else [Vector((v.x*.5,v.y*.5)) for v in [a,b,c]]
        for li in face.loop_indices:
            p=mesh.vertices[mesh.loops[li].vertex_index].co;ap=p-a
            if abs(den)>1e-22:
                v=(d11*ap.dot(ab)-d01*ap.dot(ac))/den;w=(d00*ap.dot(ac)-d01*ap.dot(ab))/den
                value=coords[0]*(1-v-w)+coords[1]*v+coords[2]*w
            else:value=Vector((p.x*.5,p.y*.5))
            uv.data[li].uv=value
            if custom_normals is not None:
                if abs(den)>1e-22:
                    normal=source.corner_normals[target.loops[0]].vector*(1-v-w)+source.corner_normals[target.loops[1]].vector*v+source.corner_normals[target.loops[2]].vector*w
                    custom_normals[li]=normal.normalized() if normal.length>1e-10 else face.normal.copy()
                else:custom_normals[li]=face.normal.copy()
        values=[uv.data[i].uv for i in face.loop_indices]
        area=abs((values[1]-values[0]).cross(values[2]-values[0]))
        if area<1e-14:
            dominant=max(range(3),key=lambda k:abs(face.normal[k]));axes=[k for k in range(3) if k!=dominant]
            for li in face.loop_indices:
                p=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=(p[axes[0]]*.5,p[axes[1]]*.5)
    if custom_normals is not None:mesh.normals_split_custom_set(custom_normals)
    mesh.calc_loop_triangles();assert len(mesh.loop_triangles)<=g['budget']<=200
    return mesh

catalog_assets=[];mapping={};sources={};new_objects={};report={'revision':REVISION,'reference':plan['reference'],'parts':{},'instances_refreshed':0}
for index,(name,p) in enumerate(plan['parts'].items()):
    old=prototypes[name];instances=[o for o in original_objects if o.type=='MESH' and o.get('asset_id')==name]
    palette_by_object={o:tuple(o.data.materials) for o in instances}
    bindings={o:list({tuple(sorted((o.vertex_groups[g.group].name,round(g.weight,7)) for g in v.groups)) for v in o.data.vertices}) for o in instances}
    groups=p['groups'];mapping[name]=[g['id'] for g in groups]
    sources[name]=dict(points=[list(v.co) for v in old.data.vertices],triangles=[list(t.vertices) for t in old.data.loop_triangles])
    # Build all parts before replacing the parent's mesh; UV lookup uses the old source.
    built=[new_mesh(old,g) for g in groups]
    for j,(g,mesh) in enumerate(zip(groups,built)):
        ob=old if j==0 else old.copy()
        if j:
            ob.name=g['id']
            for c in old.users_collection:c.objects.link(ob)
        ob.data=mesh;copy_weights(ob,g)
        ob['asset_id']=g['id'];ob['budget_revision']=REVISION;ob['budget_parent']=name
        ob['functional_role']=g['label'];ob['max_triangles']=g['budget'];ob['triangle_count']=len(g['triangles'])
        ob['budget_size_diagonal_m']=g['diagonal_m'];ob['source_triangles_before_budget']=p['source_triangles']
        ob.asset_mark();ob.asset_data.description=f"{original[name]['family']} / {g['label']} / {len(g['triangles'])} of {g['budget']} triangles"
        new_objects[g['id']]=ob
        a={**original[name],'id':g['id'],'vertices':len(mesh.vertices),'polygons':len(mesh.polygons),'triangles':len(g['triangles']),
           'blender_object':ob.name,'budget_revision':REVISION,'triangle_budget':g['budget'],'budget_parent':name,'functional_role':g['label'],
           'size_diagonal_m':g['diagonal_m'],'max_surface_error_m':g['surface_error_m']}
        catalog_assets.append(a)
        report['parts'][g['id']]={'parent':name,'role':g['label'],'triangles':len(g['triangles']),'budget':g['budget'],'bounds':g['bounds'],
                                'method':g['method'],'surface_error_m':g['surface_error_m'],'source_object':ob.name}
    for instance in instances:
        if instance==old:continue
        palette=palette_by_object[instance]
        for j,g in enumerate(groups):
            ob=instance if j==0 else instance.copy()
            if j:
                ob.name=instance.name+'__'+g['label']
                for c in instance.users_collection:c.objects.link(ob)
            ob.data=built[j].copy();copy_instance_weights(ob,g,bindings[instance])
            ob.data.materials.clear()
            for mat in palette:ob.data.materials.append(mat)
            ob['asset_id']=g['id'];ob['budget_revision']=REVISION;ob['budget_parent']=name;ob['functional_role']=g['label']
            ob['triangle_count']=len(g['triangles']);ob['max_triangles']=g['budget'];ob['budget_size_diagonal_m']=g['diagonal_m']
            report['instances_refreshed']+=1
    if index%250==0:print('APPLY_SIZE_BUDGET',index,'/',len(plan['parts']),flush=True)

# The bank has no animations. Strip all pose evaluation temporarily and restore
# it before saving the master; this leaves every existing review clip intact.
bank_scene=bpy.data.scenes.new('Budget_Geometry_Export');bank_scene.render.fps=30
bpy.context.window.scene=bank_scene;rigs=set();saved={}
for ob in new_objects.values():
    bank_scene.collection.objects.link(ob)
    for modifier in ob.modifiers:
        if modifier.type=='ARMATURE' and modifier.object:
            rig=modifier.object;rigs.add(rig)
            if rig.name not in bank_scene.objects:bank_scene.collection.objects.link(rig)
    parent=ob.parent
    while parent:
        if parent.name not in bank_scene.objects:bank_scene.collection.objects.link(parent)
        if parent.type=='ARMATURE':rigs.add(parent)
        parent=parent.parent
for rig in rigs:
    ad=rig.animation_data
    saved[rig]=(ad.action if ad else None,ad.action_slot if ad else None,ad.use_nla if ad else False,{b.name:b.matrix_basis.copy() for b in rig.pose.bones})
    if ad:ad.action=None;ad.use_nla=False
    for b in rig.pose.bones:b.matrix_basis=Matrix.Identity(4)
for ob in bank_scene.objects:ob.select_set(True)
bpy.context.view_layer.update()
bank=ROOT/'Validation/Budget_Geometry.glb';stage=bank.with_name('Budget_Geometry.staging.glb')
bpy.ops.export_scene.gltf(filepath=str(stage),export_format='GLB',use_selection=True,use_active_scene=True,export_animations=False,
                          export_skins=True,export_def_bones=False,export_yup=True,export_extras=True,export_materials='EXPORT',
                          export_apply=False,export_texcoords=True,export_normals=True,export_tangents=True)
stage.replace(bank)
for rig,(action,slot,use_nla,bases) in saved.items():
    if rig.animation_data:
        rig.animation_data.action=action
        if action and slot:rig.animation_data.action_slot=slot
        rig.animation_data.use_nla=use_nla
    for b in rig.pose.bones:b.matrix_basis=bases[b.name]
bpy.context.window.scene=bpy.data.scenes[entry];bpy.data.scenes.remove(bank_scene)

# One authoritative manifest drives source instances, GLB rewriting and assembly
# recipes. The unchanged collision contracts remain separately retained.
cat['assets']=catalog_assets;cat['triangles_per_part_limit']=200;cat['part_budget_revision']=REVISION
assert all(ids==[n] for n,ids in mapping.items()),'No new part IDs may be generated'
cat.pop('functional_part_companions',None)
(ROOT/'Validation/size_budget_catalog_staged.json').write_text(json.dumps(cat,indent=2)+'\n',encoding='utf-8')
(ROOT/'Validation/size_budget_mapping.json').write_text(json.dumps(mapping,indent=2)+'\n',encoding='utf-8')
report['summary']=dict(original_parts=len(plan['parts']),parts=len(catalog_assets),source_triangles=sum(p['source_triangles'] for p in plan['parts'].values()),
                       final_triangles=sum(a['triangles'] for a in catalog_assets),max_triangles=max(a['triangles'] for a in catalog_assets))
(ROOT/'Validation/size_budget_source_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'),compress=True)
print('SIZE_BUDGET_SOURCE_SAVED',json.dumps(report['summary']),flush=True)
