"""Regression checks for hidden-face safety and the actual saved detail sources."""
import sys
import json
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'Tools'))
from detail_mesh_pipeline import _occluded_faces


def nested_box_case(center=(0,0,0),same_bone=True,opaque=True):
    bm=bmesh.new()
    layer=bm.faces.layers.int.new('authoring_component')
    for index,(position,size) in enumerate([((0,0,0),2),(center,.8)]):
        before=set(bm.faces)
        created=bmesh.ops.create_cube(bm,size=size)
        for v in created['verts']:
            v.co+=Vector(position)
        for f in set(bm.faces)-before:
            f[layer]=index
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    deleted=_occluded_faces(bm,['hull','hull' if same_bone else 'gear.L'],{0,1} if opaque else {1})
    result=[f[layer] for f in deleted]
    bm.free()
    return result


cases={}
result=nested_box_case()
assert len(result)==12 and set(result)=={1},result
cases['opaque_same_bone_inner_box_removed']=True
assert not nested_box_case(same_bone=False),'faces that a moving joint can expose were removed'
cases['different_joint_faces_preserved']=True
assert not nested_box_case(opaque=False),'transparent outer shell occluded visible geometry'
cases['transparent_occluder_excluded']=True
result=nested_box_case(center=(.9,0,0))
assert len(result)==2 and set(result)=={1},result
cases['partially_exposed_surface_preserved']=True

bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'))
report=json.loads((ROOT/'Validation/detail_pipeline_report.json').read_text(encoding='utf-8'))
assert len(report['parts'])==219
for entry in report['parts']:
    raw=bpy.data.objects[entry['source_object']]
    final=bpy.data.objects[entry['id']]
    raw.data.calc_loop_triangles()
    final.data.calc_loop_triangles()
    assert len(raw.data.loop_triangles)==entry['source_triangles']
    assert len(final.data.loop_triangles)==entry['optimized_triangles']<=500
    assert raw.data is not final.data,'source was overwritten by optimization'
    assert len(raw.vertex_groups)==len(final.vertex_groups),'source joint assignment lost'
    assert final.data.uv_layers and raw.data.uv_layers
    assert entry['max_surface_error_m']<5e-5
assert bpy.context.scene.name=='16_Transit_Connected_Example','library entry scene changed'
result=dict(cases=cases,saved_part_pairs_checked=len(report['parts']),all_checks_passed=True)
(ROOT/'Validation/detail_pipeline_guard_report.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('DETAIL_PIPELINE_GUARDS',json.dumps(result),flush=True)
