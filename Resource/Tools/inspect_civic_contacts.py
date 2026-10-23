import bpy,sys,math
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from civic_motion import human_pose
scene=bpy.data.scenes['00_Library'];bpy.context.window.scene=scene
rig=bpy.data.objects['HUMAN_7H'];a=bpy.data.actions['HUMAN_7H__Walk']
print('ACTION_RANGE',a.frame_range[:],len(a.fcurves),scene.render.fps)
print('LENGTHS',[(n,rig.data.bones[n].length,tuple(rig.data.bones[n].head_local),tuple(rig.data.bones[n].tail_local)) for n in ['thigh.L','shin.L']])
rig.animation_data.action=a;rig.animation_data.action_slot=a.slots[0];rig.animation_data.use_nla=False
for t in [0,.13,.2,.3,.4,.5,.6,.8,1]:
    f=30*t;scene.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update()
    b=rig.pose.bones['foot.L'];defo=b.matrix@b.bone.matrix_local.inverted()
    p=human_pose(rig,'Walk',t)
    if t==.13:
        print('CHAIN',[(n,tuple(p.fk(n).translation)) for n in ['pelvis','thigh.L','shin.L','foot.L']])
    print('CONTACT',t,'foot',tuple(b.matrix.translation),'heel',tuple(defo@Vector((.115,.065,0))),
          'raw',tuple(p.point('foot.L',(.115,.065,0))))
