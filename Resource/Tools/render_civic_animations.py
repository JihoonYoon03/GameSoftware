"""Render actual skinned motion, not synthetic illustration."""
import bpy,json,sys,math,os
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
scene=bpy.data.scenes['37_Civic_Animation_Review'];bpy.context.window.scene=scene
rig=bpy.data.objects['Civic_Review_Rig']
scene.render.resolution_x=560;scene.render.resolution_y=560
scene.render.resolution_percentage=100;scene.cycles.samples=6
if '--size' in sys.argv:
    scene.render.resolution_x=scene.render.resolution_y=int(sys.argv[sys.argv.index('--size')+1])
if '--samples' in sys.argv:scene.cycles.samples=int(sys.argv[sys.argv.index('--samples')+1])
folder=ROOT/'Preview/Civic';folder.mkdir(parents=True,exist_ok=True)
def render_to(s,path):
    path=Path(path);stage=path.with_name(path.stem+'.rendering_'+str(os.getpid())+'.png')
    s.render.filepath=str(stage);bpy.ops.render.render(write_still=True)
    stage.replace(path)
clips=['Idle','Walk','Run','Turn_Left_90','Attack_Melee','Attack_Fire','Jump_Full',
       'Hit_Front','Knockdown_Front','Knockdown_Back','Knockdown_Left','Knockdown_Right',
       'Downed_Loop','GetUp_Front','GetUp_Back','GetUp_Left','GetUp_Right','Death_Front','Death_Back',
       'Flee_Start','Flee_Run','Flee_Stop','Medical_Stabilized','Medical_GetUp','Security_StunFire','Rider_Brace']
if '--directional' in sys.argv:
    clips += [c['clip'] for c in json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))['HUMAN_7H'] if c['clip'].startswith(('Aim_Walk_','Aim_Run_'))]
if '--quick' in sys.argv:clips=['Walk','Run','Knockdown_Front','GetUp_Front','Attack_Fire','Rider_Brace']
if '--clips' in sys.argv:clips=sys.argv[sys.argv.index('--clips')+1].split(',')
count=7 if '--quick' in sys.argv else 19
if '--count' in sys.argv:count=int(sys.argv[sys.argv.index('--count')+1])
if '--scenes-only' in sys.argv:
    for name,clip,end in [('38_Motion_Before_After','Before_After',30),('40_Bike_Rider_Contact_Review','Bike_Contact',15)]:
        s=bpy.data.scenes[name];bpy.context.window.scene=s;s.cycles.samples=8;s.render.resolution_percentage=65
        for i in range(13):
            f=end*i/12;s.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update()
            render_to(s,folder/f'{clip}_{i:02}.png')
    # Reuse the paired actors for the second gait without saving source changes.
    s=bpy.data.scenes['38_Motion_Before_After'];bpy.context.window.scene=s
    for ob in s.objects:
        if ob.type!='ARMATURE':continue
        prefix='UpperBodyV1__' if 'Before' in ob.name else ''
        a=bpy.data.actions[prefix+'HUMAN_7H__Run'];ob.animation_data.action=a;ob.animation_data.action_slot=a.slots[0];ob.animation_data.use_nla=False;ob.update_tag()
    for i in range(13):
        f=20*i/12;s.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update();render_to(s,folder/f'Before_After_Run_{i:02}.png')
    for name,file in [('39_Civic_Body_Profile_Review','body_profiles.png'),('41_Vehicle_Impact_Review','vehicles.png'),('42_Aim_Directional_Review','directional_overview.png')]:
        s=bpy.data.scenes[name];bpy.context.window.scene=s;s.cycles.samples=12
        render_to(s,folder/file)
    print('CIVIC_REVIEW_SCENES_RENDERED',flush=True)
    raise SystemExit
for clip in clips:
    for ob in scene.objects:
        if ob.get('asset_id')=='Weapon_CoilPistol_35cm':ob.hide_render=clip in ['Rider_Brace','Ride_Base']
    action=bpy.data.actions['HUMAN_7H__'+clip]
    rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0];rig.animation_data.use_nla=False
    end=action.frame_range[1]
    for i in range(count):
        f=end*i/(count-1);scene.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update()
        render_to(scene,folder/f'{clip}_{i:02}.png')
print('CIVIC_PREVIEWS_RENDERED',clips,count,flush=True)
