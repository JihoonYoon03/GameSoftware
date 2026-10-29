"""Verify Sprint support against its declared 4.2m/s Actor reference speed."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
rig=bpy.data.objects['HUMAN_7H'];scene=bpy.data.scenes.new('Acceleration_Stride_QA')
scene.render.fps=30;scene.collection.objects.link(rig);bpy.context.window.scene=scene
a=bpy.data.actions['HUMAN_7H__Sprint'];rig.animation_data.action=a;rig.animation_data.action_slot=a.slots[0];rig.animation_data.use_nla=False
result={}
for label,start,end,pivot in [('heel',.09,.18,.065),('toe',.24,.31,-.195)]:
    samples=[]
    for i in range(21):
        t=start+(end-start)*i/20;f=15*t;scene.frame_set(math.floor(f),subframe=f%1);bpy.context.view_layer.update()
        e=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
        p=e.pose.bones['foot.L'].matrix@rig.data.bones['foot.L'].matrix_local.inverted()@Vector((.115,pivot,0))
        p.y-=4.2*.5*t;samples.append(p)
    drift=max(math.hypot(p.x-samples[0].x,p.y-samples[0].y) for p in samples)
    assert drift<.003,(label,drift)
    result[label]=dict(phase_range=[start,end],compensated_horizontal_drift_m=drift)
path=ROOT/'Validation/acceleration_motion_report.json';report=json.loads(path.read_text())
report['sprint_support']=result;path.write_text(json.dumps(report,indent=2)+'\n')
print('ACCELERATION_SPRINT_SUPPORT_PASSED',json.dumps(result),flush=True)
