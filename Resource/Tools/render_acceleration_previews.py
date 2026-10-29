"""Render real rig deformation in the established review studio, preserving source files."""
import bpy,json,sys,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from acceleration_motion import SPECS
scene=bpy.data.scenes['37_Civic_Animation_Review'];bpy.context.window.scene=scene
rig=bpy.data.objects['Civic_Review_Rig'];scene.render.resolution_x=scene.render.resolution_y=400
scene.render.resolution_percentage=100;scene.cycles.samples=4
for ob in scene.objects:
    if ob.get('asset_id')=='Weapon_CoilPistol_35cm':ob.hide_render=True
folder=ROOT/'Preview/Acceleration';folder.mkdir(parents=True,exist_ok=True)
selected=list(SPECS)
if '--clips' in sys.argv:selected=sys.argv[sys.argv.index('--clips')+1].split(',')
for name in selected:
    action=bpy.data.actions['HUMAN_7H__'+name]
    rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0];rig.animation_data.use_nla=False;rig.update_tag()
    for i in range(9):
        f=action.frame_range[1]*i/8;scene.frame_set(math.floor(f),subframe=f%1);bpy.context.view_layer.update()
        scene.render.filepath=str(folder/f'{name}_{i:02}.png');bpy.ops.render.render(write_still=True)
    print('ACCELERATION_RENDERED',name,flush=True)
