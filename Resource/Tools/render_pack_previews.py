"""Render the saved pack without rebuilding or exporting geometry."""
import bpy,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
device='CPU'
try:
    p=bpy.context.preferences.addons['cycles'].preferences;p.compute_device_type='OPTIX';p.get_devices()
    for d in p.devices:d.use=d.type=='OPTIX'
    if any(d.type=='OPTIX' for d in p.devices):device='GPU'
except Exception:pass
def render(name,file,percentage=100,samples=24):
    s=bpy.data.scenes[name];bpy.context.window.scene=s;s.cycles.device=device;s.cycles.samples=samples
    s.render.resolution_percentage=percentage;s.render.filepath=str(ROOT/'Preview'/file)
    bpy.ops.render.render(write_still=True,scene=s.name)
jobs=[('07_Body_Profiles','06_body_profiles.png'),('08_Outfit_Purposes','07_outfit_purposes.png'),
 ('08b_Outfit_Silhouette_Check','08_outfits_grayscale.png'),('09_Vehicles','09_vehicles.png'),
 ('09b_Vehicle_Variants','10_vehicle_variants.png'),('09c_Vehicle_Exploded','11_vehicle_exploded.png'),
 ('01_Characters','01_characters.png'),('03_Halo_Settlement','03_halo_settlement.png'),('04_Interior_Kit','04_interior.png')]
if '--turn-only' in sys.argv:jobs=[]
for name,file in jobs:render(name,file)
if '--static-only' in sys.argv:
    print('STATIC_PREVIEWS_DONE');raise SystemExit(0)
clips=json.loads((ROOT/'Documentation/animation_clips.json').read_text())
scene=bpy.data.scenes['06_Animation_Review'];bpy.context.window.scene=scene
rigs=[o for o in scene.objects if o.type=='ARMATURE']
motion_clips=['Turn_Left_90'] if '--turn-only' in sys.argv else ['Walk','Run','Turn_Left_90','Attack_Melee','Attack_Fire','Jump_Full']
for clip in motion_clips:
    for rig in rigs:
        source='HUMAN_7H' if rig.name=='Review_Human_Rig' else rig.name.replace('Review_','').removesuffix('_Rig')
        action=bpy.data.actions[source+'__'+clip];rig.animation_data.action=action
        rig.animation_data.action_slot=action.slots[0];rig.animation_data.use_nla=False;rig.update_tag()
    end=next(c['end'] for c in clips['HUMAN_7H'] if c['clip']==clip)
    for i in range(8):
        scene.frame_set(round(end*i/7));render(scene.name,f'animation_{clip}_{i:02}.png',60,16)
if '--turn-only' in sys.argv:
    print('TURN_PREVIEWS_DONE');raise SystemExit(0)
vscene=bpy.data.scenes['09_Vehicles'];bpy.context.window.scene=vscene
vrigs=[o for o in vscene.objects if o.type=='ARMATURE']
for i in range(14):
    is_open=i<7;phase=i/6 if is_open else (i-7)/6;clip='Canopy_Open' if is_open else 'Canopy_Close'
    for rig in vrigs:
        action=bpy.data.actions['VEHICLE_'+rig['vehicle_type']+'__'+clip]
        rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0];rig.animation_data.use_nla=False;rig.update_tag()
    vscene.frame_set(round(phase*30));render(vscene.name,f'vehicle_access_{i:02}.png',55,16)
print('ALL_PREVIEWS_DONE')
