"""Publish an approved four-action draft and a small animation-only transfer bank."""
import bpy,json,sys,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import Pose
from pistol_layer_motion import SPECS
qa=json.loads((ROOT/'Validation/pistol_layer_validation.json').read_text());assert qa['all_checks_passed']
proof=json.loads((ROOT/'Validation/pistol_layer_authoring.json').read_text())
master=ROOT/'Source/Erma_Resource_Library.blend';assert hashlib.sha256(master.read_bytes()).hexdigest()==proof['master_sha256'],'Master changed since the reviewed draft'
rig=bpy.data.objects['HUMAN_7H'];entry='16_Transit_Connected_Example'
# Export a temporary rig with only the four new tracks, avoiding a full rebuild
# of the existing 59 actions or any character geometry.
scene=bpy.data.scenes.new('Pistol_Animation_Bank');scene.render.fps=60;bpy.context.window.scene=scene
bankrig=rig.copy();bankrig.data=rig.data.copy();bankrig.name='Pistol_Animation_Bank_Rig';scene.collection.objects.link(bankrig)
bankrig.animation_data_clear();bankrig.animation_data_create();bankrig.animation_data.use_nla=True
Pose(bankrig).apply()
for name in SPECS:
    tr=bankrig.animation_data.nla_tracks.new();tr.name=name;st=tr.strips.new(name,0,bpy.data.actions['HUMAN_7H__'+name]);st.name=name;st.scale=2
weapon=bpy.data.objects['Weapon_CoilPistol_35cm'].copy();weapon.name='Pistol_Bank_Weight_Reference';scene.collection.objects.link(weapon);weapon.parent=bankrig
for mod in weapon.modifiers:
    if mod.type=='ARMATURE':mod.object=bankrig
for ob in scene.objects:ob.select_set(True)
bpy.context.view_layer.objects.active=bankrig;scene.frame_set(0)
bpy.ops.export_scene.gltf(filepath=str(ROOT/'Validation/Pistol_Animation_Bank.glb'),export_format='GLB',use_selection=True,use_active_scene=True,
    export_animations=True,export_animation_mode='NLA_TRACKS',export_nla_strips=True,export_force_sampling=True,export_frame_range=False,export_frame_step=1,
    export_skins=True,export_def_bones=False,export_yup=True,export_extras=True,export_materials='EXPORT',export_apply=False,
    export_texcoords=True,export_normals=True,export_tangents=True,export_anim_slide_to_zero=True,export_optimize_animation_size=True)
data=bankrig.data;bpy.data.objects.remove(weapon,do_unlink=True);bpy.data.objects.remove(bankrig,do_unlink=True)
if not data.users:bpy.data.armatures.remove(data)
bpy.context.window.scene=bpy.data.scenes[entry];bpy.data.scenes.remove(scene)
names=set(rig.data.bones.keys())
for actor in list(bpy.data.objects):
    if actor.type!='ARMATURE' or set(actor.data.bones.keys())!=names or actor==rig:continue
    actor.animation_data_create()
    for name in SPECS:
        for tr in list(actor.animation_data.nla_tracks):
            if tr.name==name:actor.animation_data.nla_tracks.remove(tr)
        tr=actor.animation_data.nla_tracks.new();tr.name=name;st=tr.strips.new(name,0,bpy.data.actions['HUMAN_7H__'+name]);st.name=name;tr.mute=True
path=ROOT/'Documentation/animation_clips.json';meta=json.loads(path.read_text(encoding='utf-8'));original=[c for c in meta['HUMAN_7H'] if c['clip'] not in SPECS]
for name,(base,end,loop) in SPECS.items():
    events=[]
    if name=='Reload_Pistol':
        events=[{'phase':phase,'seconds':round(1.4*phase,6),'event':event,'visual_only':True,'changes_ammo':False} for phase,event in [(.30,'reload_magazine_release_visual'),(.67,'reload_magazine_seat_visual'),(.80,'reload_slide_release_visual'),(1,'reload_pose_recovered')]]
    meta_entry=dict(clip=name,action='HUMAN_7H__'+name,fps=30,start=0,end=end,duration_seconds=end/30,loop=loop,
        root_translation='in_place',root_yaw_degrees=0,layer='human_upper_body',mask='human_upper_body',blend_mode='masked_override',
        reference=base,phase_sync_group='human_locomotion' if loop else 'reload',matched_locomotion=base if loop else None,
        blend_in_seconds=.15,blend_out_seconds=.15,events=events,revision='pistol_layers_v1',authoring_fps=30,delivery_sampling_fps=60,
        support='lower body supplied by the existing movement layer; right hand owns the weapon grip',
        entry_pose='Pistol_Carry_Idle:0' if not loop else name+':0',exit_pose='Pistol_Carry_Idle:0' if not loop else name+':0')
    original.append(meta_entry)
meta['HUMAN_7H']=original;path.write_text(json.dumps(meta,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
rig.animation_data.action=None;rig.animation_data.use_nla=False;Pose(rig).apply()
bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(master),compress=True)
print('PISTOL_SOURCE_PUBLISHED',len(original),'CLIPS',flush=True)
