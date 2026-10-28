"""Reopen the saved source and reimport representative transit GLBs in Blender."""
import json
from pathlib import Path
import bpy

ROOT=Path(__file__).resolve().parents[1]
KIT=json.loads((ROOT/'Documentation/transit_modules.json').read_text(encoding='utf-8'))['modules']
report=dict(source_reopened=False,source_parts_checked=0,files={},errors=[])
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'))
source_bounds={}
try:
    assert bpy.data.scenes.get('16_Transit_Connected_Example')
    for n,m in KIT.items():
        coords=[]
        for part in m['parts']:
            o=bpy.data.objects[part];assert o.type=='MESH' and o.get('expansion')=='transit_kit_v1'
            assert o.data.uv_layers
            o.data.calc_loop_triangles();assert 0<len(o.data.loop_triangles)<=500
            coords.extend(tuple(v.co) for v in o.data.vertices)
            report['source_parts_checked']+=1
        source_bounds[n]=[[min(p[i] for p in coords),max(p[i] for p in coords)] for i in range(3)]
    report['source_reopened']=True
except Exception as e:report['errors'].append('source: '+str(e))

names=['Transit_Road_2L_Curve15_R18','Transit_Road_4L_Curve45_R36','Transit_Road_6L_Curve90_R18',
       'Transit_Road_2L_XJunction_R4','Transit_Road_6L_TJunction_R4',
       'Transit_Road_Taper_2L_4L_24m','Transit_Road_Taper_4L_6L_24m','Transit_Road_Taper_2L_6L_36m',
       'Transit_Walk_W2_Curve90_R4','Transit_Walk_W4_XJunction',
       'Transit_Walk_W2_AccessibleRamp_4m','Transit_Walk_W4_AccessibleRamp_4m','Transit_Walk_W4_EndCap',
       'Transit_Road_6L_Crosswalk_4m','Transit_AirGuide_Straight_32m','Transit_AirGuide_Curve90_R32',
       'Transit_AirGuide_Curve15_R64','Transit_AirGuide_XJunction']
for n in names:
    try:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.gltf(filepath=str(ROOT/'Export/Transit'/f'{n}.glb'))
        obs=[o for o in bpy.context.scene.objects if o.type=='MESH']
        assert len(obs)==len(KIT[n]['parts']),'mesh count mismatch'
        coords=[];triangles=0
        for o in obs:
            assert o.data.uv_layers
            assert o.name in KIT[n]['parts']
            o.data.calc_loop_triangles();tc=len(o.data.loop_triangles)
            assert 0<tc<=500
            triangles+=tc
            coords.extend(tuple(o.matrix_world@v.co) for v in o.data.vertices)
        bounds=[[min(p[i] for p in coords),max(p[i] for p in coords)] for i in range(3)]
        err=max(abs(bounds[i][j]-source_bounds[n][i][j]) for i in range(3) for j in range(2))
        assert err<2e-5,('axis or scale mismatch',err)
        report['files'][n]=dict(meshes=len(obs),triangles=triangles,max_bounds_error_m=err,passed=True)
    except Exception as e:report['errors'].append(n+': '+str(e))
report['files_checked']=len(report['files'])
report['all_checks_passed']=not report['errors'] and len(report['files'])==len(names)
(ROOT/'Validation/transit_roundtrip_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('TRANSIT_ROUNDTRIP',json.dumps({k:v for k,v in report.items() if k!='files'}),flush=True)
assert report['all_checks_passed'],report['errors']
