"""Render actual skinned motion, not synthetic illustration."""
import bpy,json,sys,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
scene=bpy.data.scenes['37_Civic_Animation_Review'];bpy.context.window.scene=scene
rig=bpy.data.objects['Civic_Review_Rig']
scene.render.resolution_x=560;scene.render.resolution_y=560
scene.render.resolution_percentage=100;scene.cycles.samples=6
folder=ROOT/'Preview/Civic';folder.mkdir(parents=True,exist_ok=True)
clips=['Idle','Walk','Run','Turn_Left_90','Attack_Melee','Attack_Fire','Jump_Full',
       'Hit_Front','Knockdown_Front','Knockdown_Back','Knockdown_Left','Knockdown_Right',
       'Downed_Loop','GetUp_Front','GetUp_Back','Death_Front','Flee_Run','Security_StunFire','Rider_Brace']
if '--quick' in sys.argv:clips=['Walk','Run','Knockdown_Front','GetUp_Front','Attack_Fire','Rider_Brace']
if '--clips' in sys.argv:clips=sys.argv[sys.argv.index('--clips')+1].split(',')
count=7 if '--quick' in sys.argv else 19
if '--count' in sys.argv:count=int(sys.argv[sys.argv.index('--count')+1])
for clip in clips:
    action=bpy.data.actions['HUMAN_7H__'+clip]
    rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0];rig.animation_data.use_nla=False
    end=action.frame_range[1]
    for i in range(count):
        f=end*i/(count-1);scene.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update()
        scene.render.filepath=str(folder/f'{clip}_{i:02}.png')
        bpy.ops.render.render(write_still=True)
print('CIVIC_PREVIEWS_RENDERED',clips,count,flush=True)
