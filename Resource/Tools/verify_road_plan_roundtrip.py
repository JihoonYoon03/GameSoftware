"""Re-import every road-plan assembly and inspect both editable blend deliveries."""
import hashlib
import json
from pathlib import Path
import bpy

ROOT=Path(__file__).resolve().parents[1]
pipeline=json.loads((ROOT/'Validation/road_plan_pipeline_report.json').read_text(encoding='utf-8'))
kit=json.loads((ROOT/'Documentation/road_plan_modules.json').read_text(encoding='utf-8'))
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
report=dict(source_pairs=0,reimported_assemblies=0,reimported_mesh_nodes=0,max_reimport_bounds_error_m=0,errors=[])
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'))
assert bpy.context.scene.name=='16_Transit_Connected_Example'
canonical={o.get('asset_id'):o for o in bpy.data.collections['20_RoadPlan_Modules'].all_objects if o.get('asset_id')}
assert len(canonical)==len(pipeline['parts'])
for row in pipeline['parts']:
    raw=bpy.data.objects[row['source_object']];final=canonical[row['id']]
    assert raw.data is not final.data and raw.data.uv_layers and final.data.uv_layers
    raw.data.calc_loop_triangles();final.data.calc_loop_triangles()
    assert len(raw.data.loop_triangles)==row['source_triangles']
    assert len(final.data.loop_triangles)==row['optimized_triangles']<=500
    assert row['max_surface_error_m']<5e-5
    assert tuple(final.location)==(0,0,0) and tuple(final.scale)==(1,1,1)
    assert final.parent is None
    report['source_pairs']+=1
digest=hashlib.sha256()
for a in sorted(catalog['assets'],key=lambda a:a['id']):
    if a.get('expansion')=='road_plan_v1':continue
    o=bpy.data.objects[a['id']];digest.update(a['id'].encode())
    digest.update(json.dumps([[tuple(v.co) for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons],
                              [m.name for m in o.data.materials]],separators=(',',':')).encode())
assert digest.hexdigest()==pipeline['protected_previous_geometry_sha256']
report['existing_geometry_preserved']=True

for name,info in kit['modules'].items():
    try:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.gltf(filepath=str(ROOT/f'Export/RoadPlan/{name}.glb'))
        bpy.context.view_layer.update();objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
        assert {o.get('asset_id') for o in objects}==set(info['parts'])
        assert not any(o.type=='ARMATURE' for o in bpy.context.scene.objects)
        points=[]
        for o in objects:
            assert o.data.uv_layers and o.data.materials and o.get('collision_role')
            o.data.calc_loop_triangles();assert len(o.data.loop_triangles)<=500
            assert all(s>0 for s in o.scale)
            points.extend(o.matrix_world@v.co for v in o.data.vertices)
        actual=dict(min=[min(p[k] for p in points) for k in range(3)],max=[max(p[k] for p in points) for k in range(3)])
        err=max(abs(actual[b][k]-info['bounds'][b][k]) for b in ['min','max'] for k in range(3))
        report['max_reimport_bounds_error_m']=max(report['max_reimport_bounds_error_m'],err)
        assert err<.001,(name,'units or axes changed',err)
        report['reimported_assemblies']+=1;report['reimported_mesh_nodes']+=len(objects)
    except Exception as exc:report['errors'].append(f'{name}: {exc}')

bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source/RoadPlan_Modules.blend'))
assert bpy.context.scene.name=='29_RoadPlan_Ground_Connections','standalone opens without the intended review scene'
assert bpy.data.scenes.get('RoadPlan_Library') and bpy.data.scenes.get('36_RoadPlan_Detail_Sources')
canonical={o.get('asset_id'):o for o in bpy.data.collections['20_RoadPlan_Modules'].all_objects if o.get('asset_id')}
assert all(row['id'] in canonical for row in pipeline['parts'])
foreign=[o.name for o in bpy.data.objects if o.get('asset_id') and not o['asset_id'].startswith('RoadPlan_')]
assert not foreign,foreign[:10]
report['standalone_blend_owned_content_only']=True
report['all_checks_passed']=not report['errors']
(ROOT/'Validation/road_plan_roundtrip_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('ROAD_PLAN_ROUNDTRIP',json.dumps(report),flush=True)
assert report['all_checks_passed'],report['errors']
