"""Reopen both editable projects and exercise the new review scene."""
import bpy,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
report=json.loads((ROOT/'Validation/acceleration_project_preservation.json').read_text())
report['reopened']={}
names=set(json.loads((ROOT/'Validation/acceleration_authoring.json').read_text())['clips'])
for file,key in [('Erma_Resource_Library.blend','master_entry_scene'),('Civic_Animations.blend','civic_entry_scene')]:
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source'/file))
    assert bpy.context.scene.name==report[key]
    assert all(bpy.data.actions.get('HUMAN_7H__'+n) for n in names)
    count=len([a for a in bpy.data.actions if a.name.startswith('HUMAN_7H__') and not a.name.endswith('.001')])
    assert count==82,(file,count)
    scene=bpy.data.scenes['44_Acceleration_Review'];assert scene.camera
    bpy.context.window.scene=scene
    actors=[o for o in scene.objects if o.type=='ARMATURE'];assert len(actors)==3
    for actor in actors:
        assert len(actor.data.bones)==23 and actor.animation_data.action.name.startswith('HUMAN_7H__')
    scene.frame_set(2);bpy.context.view_layer.update()
    start={o.name:{b.name:b.matrix.copy() for b in o.pose.bones} for o in actors}
    scene.frame_set(10);bpy.context.view_layer.update()
    movement=max((b.matrix.translation-start[o.name][b.name].translation).length for o in actors for b in o.pose.bones)
    assert movement>.01
    report['reopened'][file]=dict(entry_scene=report[key],human_clips=count,review_actors=3,review_joint_movement_m=movement)
report['all_checks_passed']=True
(ROOT/'Validation/acceleration_project_preservation.json').write_text(json.dumps(report,indent=2)+'\n')
print('ACCELERATION_PROJECTS_REOPENED',json.dumps(report['reopened']),flush=True)
