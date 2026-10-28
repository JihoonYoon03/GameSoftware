"""Bind Blender 4.5 Action slots explicitly on the shared-action preview rigs."""
import bpy,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
bpy.context.preferences.filepaths.save_version=0
scene=bpy.data.scenes['06_Animation_Review'];bpy.context.window.scene=scene
clips=json.loads((ROOT/'Documentation/animation_clips.json').read_text())
rigs=[o for o in scene.objects if o.type=='ARMATURE']
def bind(clip):
    for rig in rigs:
        name='HUMAN_7H' if rig.name=='Review_Human_Rig' else rig.name.replace('Review_','').removesuffix('_Rig')
        action=bpy.data.actions[name+'__'+clip]
        rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0]
        rig.animation_data.use_nla=False;rig.update_tag()
bind('Walk');scene.frame_start=0;scene.frame_end=30;scene.frame_set(0)
bpy.context.window.scene=bpy.data.scenes['01_Characters']
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'),compress=True)
bpy.context.window.scene=scene
scene.cycles.samples=10;scene.render.resolution_percentage=60
selection=['Turn_Left_90'] if '--turn-only' in sys.argv else ['Attack_Fire','Run','Walk','Turn_Left_90','Attack_Melee','Jump_Full']
for clip in selection:
    bind(clip);end=next(c['end'] for c in clips['HUMAN_7H'] if c['clip']==clip)
    for i in range(8):
        scene.frame_set(round(end*i/7));scene.render.filepath=str(ROOT/'Preview'/f'animation_{clip}_{i:02}.png')
        bpy.ops.render.render(write_still=True,scene=scene.name)
print('ANIMATION_PREVIEWS_REFRESHED')
