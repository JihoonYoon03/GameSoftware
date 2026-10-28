"""Check every retained human source, assembly and all ten exported character rigs."""
import json
import math
import sys
import hashlib
from pathlib import Path
import bpy
from mathutils import Vector
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree

ROOT=Path(__file__).resolve().parents[1]
report=dict(source_pairs=0,max_weight_error=0,updated_actor_parts=0,body_profiles={},characters={},errors=[])
pipeline=json.loads((ROOT/'Validation/character_detail_pipeline_report.json').read_text(encoding='utf-8'))
meta=json.loads((ROOT/'Documentation/character_detail_assemblies.json').read_text(encoding='utf-8'))
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
clips=json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))['HUMAN_7H']
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'))
assert bpy.context.scene.name=='16_Transit_Connected_Example'
assert pipeline['summary']['original_parts_replaced']==785
assert len(meta['outer_assemblies'])==69
assert len(pipeline['parts'])==854
rig=bpy.data.objects['HUMAN_7H'];assert len(rig.data.bones)==23
ids={r['id'] for r in pipeline['parts']}

def weights(obj,vertex):
    return {obj.vertex_groups[x.group].name:x.weight for x in vertex.groups if x.weight>1e-7}

for row in pipeline['parts']:
    raw=bpy.data.objects[row['source_object']];final=bpy.data.objects[row['id']]
    assert raw.data is not final.data and raw.data.uv_layers and final.data.uv_layers
    raw.data.calc_loop_triangles();final.data.calc_loop_triangles()
    assert len(raw.data.loop_triangles)==row['source_triangles']
    assert len(final.data.loop_triangles)==row['optimized_triangles']<=500
    assert row['max_surface_error_m']<5e-5
    assert final.parent==rig and any(m.type=='ARMATURE' and m.object==rig for m in final.modifiers)
    kd=KDTree(len(raw.data.vertices))
    for i,v in enumerate(raw.data.vertices):
        kd.insert(v.co,i)
        assert abs(sum(x.weight for x in v.groups)-1)<1e-5,(row['id'],'raw weight sum')
    kd.balance()
    for v in final.data.vertices:
        assert abs(sum(x.weight for x in v.groups)-1)<1e-5,(row['id'],'final weight sum')
        actual=weights(final,v);near=kd.find_range(v.co,1e-6)
        assert near,(row['id'],'new vertex has no source match')
        def error(index):
            expected=weights(raw,raw.data.vertices[index])
            return max(abs(actual.get(k,0)-expected.get(k,0)) for k in set(actual)|set(expected))
        err=min(error(i) for p,i,d in near)
        report['max_weight_error']=max(report['max_weight_error'],err)
        assert err<1e-5,(row['id'],'skin differs from detailed source',err)
    report['source_pairs']+=1
for ob in bpy.data.objects:
    if ob.type=='MESH' and ob.get('asset_id') in ids:
        assert ob.get('detail_revision')=='human_detail_first_v3',ob.name
        assert all(abs(sum(x.weight for x in v.groups)-1)<1e-5 for v in ob.data.vertices),(ob.name,'saved actor skin')
        report['updated_actor_parts']+=1
eye_vertices=[]
for expression in ['Neutral','Alert','Stern']:
    ob=bpy.data.objects['Face_'+expression]
    selected={v for p in ob.data.polygons if ob.data.materials[p.material_index].name=='Eye_White' for v in p.vertices}
    eye_vertices.extend(ob.data.vertices[i].co for i in selected)
report['max_eye_surface_gap_m']=0
for label in ['Oval','Angular','Round','Narrow']:
    mesh=bpy.data.objects['Head_'+label].data
    surface=BVHTree.FromPolygons([v.co for v in mesh.vertices],[p.vertices for p in mesh.polygons])
    gap=max(surface.find_nearest(p)[3] for p in eye_vertices)
    report['max_eye_surface_gap_m']=max(report['max_eye_surface_gap_m'],gap)
    assert gap<.018,(label,'eye layer floats in front of face',gap)
digest=hashlib.sha256()
for a in sorted(catalog['assets'],key=lambda a:a['id']):
    if a.get('skeleton')=='HUMAN_7H':continue
    o=bpy.data.objects[a['id']]
    digest.update(a['id'].encode())
    digest.update(json.dumps([[tuple(v.co) for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons],
                              [m.name for m in o.data.materials]],separators=(',',':')).encode())
assert digest.hexdigest()==pipeline['protected_nonhuman_geometry_sha256'],'another expansion was changed'
report['other_expansions_preserved']=True

for build in meta['body_profiles']:
    result={}
    for label,part in [('hip','Pelvis'),('waist','Abdomen'),('chest','Chest')]:
        points=[v.co for v in bpy.data.objects[f'Body_{build}_{part}'].data.vertices]
        if label=='waist':points=[v for v in points if abs(v.z-1.13)<.005]
        result[label+'_width_m']=max(v.x for v in points)-min(v.x for v in points)
        result[label+'_depth_m']=max(v.y for v in points)-min(v.y for v in points)
    report['body_profiles'][build]=result
for sex in ['Male','Female']:
    metrics=[report['body_profiles'][sex+'_'+mass] for mass in ['Lean','Normal','Heavy']]
    for key in ['waist_width_m','waist_depth_m']:assert metrics[0][key]<metrics[1][key]<metrics[2][key]
for mass in ['Lean','Normal','Heavy']:
    assert report['body_profiles']['Male_'+mass]['chest_width_m']>report['body_profiles']['Female_'+mass]['chest_width_m']
    assert report['body_profiles']['Female_'+mass]['hip_width_m']>report['body_profiles']['Male_'+mass]['hip_width_m']
for torso,outer in meta['outer_assemblies'].items():
    assert torso in ids and outer in ids
    assert any(m.name.startswith('Cloth_') or m.name=='Rubber' for m in bpy.data.objects[torso].data.materials)
for name,info in recipes.items():
    if not isinstance(info,dict) or 'body' not in info:continue
    assert all(meta['outer_assemblies'][p] in info['parts'] for p in info['parts'] if p in meta['outer_assemblies']),name

for name,info in recipes.items():
    if not isinstance(info,dict) or 'body' not in info:continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.render.fps=30
    bpy.ops.import_scene.gltf(filepath=str(ROOT/f'Export/Characters/{name}.glb'))
    rigs=[o for o in scene.objects if o.type=='ARMATURE'];assert len(rigs)==1
    rig=rigs[0];assert len(rig.data.bones)==23
    meshes=sorted([o for o in scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)],key=lambda o:o.name)
    assert {o.get('asset_id') for o in meshes}==set(info['parts']),(name,'assembly parts lost')
    actions={a.name.split('.')[0]:a for a in bpy.data.actions}
    assert set(actions)=={c['clip'] for c in clips},(name,'clip names changed',set(actions))
    for t in rig.animation_data.nla_tracks:t.mute=True
    rig.animation_data.use_nla=False
    def sample(action,phase):
        rig.animation_data.action=action
        if action.slots:rig.animation_data.action_slot=action.slots[0]
        start,end=action.frame_range;frame=start+(end-start)*phase
        scene.frame_set(int(frame),subframe=frame-int(frame));bpy.context.view_layer.update()
        dg=bpy.context.evaluated_depsgraph_get();points=[]
        for o in meshes:
            e=o.evaluated_get(dg);m=e.to_mesh();points.extend(e.matrix_world@v.co for v in m.vertices);e.to_mesh_clear()
        return points
    result=dict(mesh_parts=len(meshes),bones=23,clips={})
    for clip,action in actions.items():
        initial=sample(action,0);motion=0;low=999;last=None
        for phase in [0,.125,.25,.5,.75,1]:
            points=sample(action,phase);last=points
            assert all(math.isfinite(x) for p in points for x in p)
            low=min(low,min(p.z for p in points));motion=max(motion,max((a-b).length for a,b in zip(initial,points)))
        assert low>=-.005,(name,clip,'ground penetration',low)
        if clip in ['Walk','Run','Jump_Full','Attack_Melee','Attack_Fire']:assert motion>1e-3,(name,clip,'frozen mesh')
        seam=max((a-b).length for a,b in zip(initial,last)) if clip in ['Walk','Run','Idle','Jump_Air'] else None
        if seam is not None:assert seam<2e-4,(name,clip,'loop seam',seam)
        result['clips'][clip]=dict(min_z_m=low,max_vertex_motion_m=motion,loop_seam_m=seam)
    report['characters'][name]=result
    print('CHARACTER_ROUNDTRIP',name,len(meshes),'PARTS',len(actions),'CLIPS',flush=True)
assert len(report['characters'])==10
report['all_checks_passed']=True
(ROOT/'Validation/character_detail_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('CHARACTER_DETAILS_VERIFIED',json.dumps({k:v for k,v in report.items() if k not in ['characters','body_profiles']}),flush=True)
