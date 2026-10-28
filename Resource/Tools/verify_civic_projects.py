"""Read-only check that both delivered Blender files open and play actual skins."""
import json
from pathlib import Path
import bpy

ROOT=Path(__file__).resolve().parents[1]
expected={c['action'] for c in json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))['HUMAN_7H']}
report={'projects':{}}
for file,entry in [('Erma_Resource_Library.blend','16_Transit_Connected_Example'),
                   ('Civic_Animations.blend','37_Civic_Animation_Review')]:
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source'/file))
    assert bpy.context.scene.name==entry,(file,bpy.context.scene.name)
    assert expected<={a.name for a in bpy.data.actions},(file,'missing actions')
    scenes=[s for s in bpy.data.scenes if s.name.startswith(('37_','38_','39_','40_','41_','42_','43_'))]
    scene_count=7 if any(n.endswith('__Reload_Pistol') for n in expected) else 6
    assert len(scenes)==scene_count
    result={'default_scene':entry,'human_actions':len(expected),'review_scenes':{}}
    if file=='Erma_Resource_Library.blend':assert len(bpy.data.objects['HUMAN_7H'].data.bones)==23
    else:assert len(bpy.data.scenes)==scene_count
    for scene in scenes:
        bpy.context.window.scene=scene
        assert scene.camera and scene.camera.name in scene.objects
        rigs=[o for o in scene.objects if o.type=='ARMATURE']
        meshes=[o for o in scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)]
        assert rigs and meshes
        for rig in rigs:
            action=rig.animation_data.action
            assert action and rig.animation_data.action_slot and not rig.animation_data.use_nla
            if 'upper_arm.L' in rig.data.bones:assert len(rig.data.bones)==23
        def sample(frame):
            scene.frame_set(-1);scene.frame_set(frame);bpy.context.view_layer.update()
            deps=bpy.context.evaluated_depsgraph_get();points=[]
            for ob in meshes:
                e=ob.evaluated_get(deps);mesh=e.to_mesh()
                points.extend(e.matrix_world@v.co for v in mesh.vertices);e.to_mesh_clear()
            return points
        first=sample(0);middle=sample(max(1,scene.frame_end//2))
        motion=max((a-b).length for a,b in zip(first,middle))
        assert motion>.001,(file,scene.name,'frozen skin',motion)
        result['review_scenes'][scene.name]={'rigs':len(rigs),'skinned_parts':len(meshes),'actual_vertex_motion_m':motion,'camera':scene.camera.name}
    report['projects'][file]=result
report['all_checks_passed']=True
(ROOT/'Validation/civic_project_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('CIVIC_PROJECTS_VERIFIED',json.dumps(report),flush=True)
