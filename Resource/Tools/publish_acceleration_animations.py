"""Publish the verified draft and append-only bank; retain both projects' entry scenes."""
import ast,bpy,json,sys,hashlib,math
from pathlib import Path
from mathutils import Vector,Matrix,Quaternion
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import Pose
from acceleration_motion import SPECS,REVISION
def action_signature(a):
    data=[(f.data_path,f.array_index,f.extrapolation,[(list(k.co),list(k.handle_left),list(k.handle_right),k.interpolation) for k in f.keyframe_points]) for f in a.fcurves]
    return hashlib.sha256(json.dumps(data,separators=(',',':')).encode()).hexdigest()
proof=json.loads((ROOT/'Validation/acceleration_authoring.json').read_text())
qa=json.loads((ROOT/'Validation/acceleration_equipment.json').read_text())
motionqa=json.loads((ROOT/'Validation/acceleration_motion_report.json').read_text())
assert proof['all_checks_passed'] and qa['all_checks_passed'] and motionqa['all_checks_passed']
master=ROOT/'Source/Erma_Resource_Library.blend'
assert hashlib.sha256(master.read_bytes()).hexdigest()==proof['master_sha256'],'Master changed during authoring'
rig=bpy.data.objects['HUMAN_7H'];entry=proof['entry_scene']
scene=bpy.data.scenes.new('Acceleration_Animation_Bank');scene.render.fps=60;bpy.context.window.scene=scene
bankrig=rig.copy();bankrig.data=rig.data.copy();bankrig.name='Acceleration_Bank_Rig';scene.collection.objects.link(bankrig)
bankrig.animation_data_clear();bankrig.animation_data_create();bankrig.animation_data.use_nla=True;Pose(bankrig).apply()
for name in SPECS:
    tr=bankrig.animation_data.nla_tracks.new();tr.name=name
    st=tr.strips.new(name,0,bpy.data.actions['HUMAN_7H__'+name]);st.name=name;st.scale=2
weapon=bpy.data.objects['Weapon_CoilPistol_35cm'].copy();weapon.name='Acceleration_Bank_Weight_Reference'
scene.collection.objects.link(weapon);weapon.parent=bankrig
for mod in weapon.modifiers:
    if mod.type=='ARMATURE':mod.object=bankrig
for o in scene.objects:o.select_set(True)
bpy.context.view_layer.objects.active=bankrig;scene.frame_set(0)
bpy.ops.export_scene.gltf(filepath=str(ROOT/'Validation/Acceleration_Animation_Bank.glb'),export_format='GLB',use_selection=True,use_active_scene=True,
    export_animations=True,export_animation_mode='NLA_TRACKS',export_nla_strips=True,export_force_sampling=True,
    export_frame_range=False,export_frame_step=1,export_skins=True,export_def_bones=False,export_yup=True,
    export_extras=True,export_materials='EXPORT',export_apply=False,export_texcoords=True,export_normals=True,
    export_tangents=True,export_anim_slide_to_zero=True,export_optimize_animation_size=True)
bpy.data.objects.remove(weapon,do_unlink=True);data=bankrig.data;bpy.data.objects.remove(bankrig,do_unlink=True)
if not data.users:bpy.data.armatures.remove(data)
bpy.context.window.scene=bpy.data.scenes[entry];bpy.data.scenes.remove(scene)
names=set(rig.data.bones.keys())
def tracks(actor):
    actor.animation_data_create()
    for name in SPECS:
        for tr in list(actor.animation_data.nla_tracks):
            if tr.name==name:actor.animation_data.nla_tracks.remove(tr)
        tr=actor.animation_data.nla_tracks.new();tr.name=name
        st=tr.strips.new(name,0,bpy.data.actions['HUMAN_7H__'+name]);st.name=name;tr.mute=True
for actor in list(bpy.data.objects):
    if actor.type=='ARMATURE' and set(actor.data.bones.keys())==names:tracks(actor)

catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
MATS={m.name:m for m in bpy.data.materials}
PROTOTYPES={a['id']:bpy.data.objects.get(a.get('blender_object',a['id'])) for a in catalog['assets']}
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ['clone_actor','studio','ground','text_label']],type_ignores=[]),'<review helpers>','exec'))
reviewname='44_Acceleration_Review'
if bpy.data.scenes.get(reviewname):
    old=bpy.data.scenes[reviewname]
    for o in list(old.objects):
        if len(o.users_scene)==1:bpy.data.objects.remove(o,do_unlink=True)
    bpy.data.scenes.remove(old)
review,col=studio(reviewname,(4,-8,3.4),(0,0,1),6.6,(1500,950));ground(col,15,-.008)
review['motion_revision']=REVISION;review.render.fps=30;review.frame_start=0;review.frame_end=30
review['instructions']='Action Editor: select HUMAN_7H__Walk_Start/Stop, Run_Start/Stop, Sprint and gait transition actions. Clips are in place; Actor speed curves are reference only.'
for label,x,clip in [('START',-1.55,'Run_Start_L'),('BRAKE',0,'Run_Stop_R'),('SPRINT',1.55,'Sprint')]:
    info=recipes['Player_Explorer'];parts=[p for p in info['parts'] if p!='Weapon_CoilPistol_35cm']
    actor,meshes=clone_actor('Acceleration_Review_'+label,rig,parts,col,(x,0,0),info.get('skin'),info.get('offsets'))
    actor.animation_data.action=bpy.data.actions['HUMAN_7H__'+clip]
    actor.animation_data.action_slot=actor.animation_data.action.slots[0];actor.animation_data.use_nla=False
    text_label(col,label,(x,-.50,-.02),.16)
review.frame_set(8)
bpy.context.window.scene=bpy.data.scenes[entry];rig.animation_data.action=None;rig.animation_data.use_nla=False;Pose(rig).apply()
assert proof['old_actions']=={n:action_signature(bpy.data.actions[n]) for n in proof['old_actions']}
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(master),compress=True)

# Update the directly openable animation project in place, retaining all its old scenes/actions.
civic=ROOT/'Source/Civic_Animations.blend';bpy.ops.wm.open_mainfile(filepath=str(civic));civic_entry=bpy.context.scene.name
old_names={a.name for a in bpy.data.actions if a.name not in {'HUMAN_7H__'+n for n in SPECS}}
old_civic_actions={n:action_signature(bpy.data.actions[n]) for n in old_names}
old_civic_scenes=set(bpy.data.scenes.keys())
if bpy.data.scenes.get(reviewname):
    old=bpy.data.scenes[reviewname]
    for o in list(old.objects):
        if len(o.users_scene)==1:bpy.data.objects.remove(o,do_unlink=True)
    bpy.data.scenes.remove(old)
for n in SPECS:
    a=bpy.data.actions.get('HUMAN_7H__'+n)
    if a:bpy.data.actions.remove(a,do_unlink=True)
with bpy.data.libraries.load(str(master),link=False) as (src,dst):
    dst.actions=['HUMAN_7H__'+n for n in SPECS];dst.scenes=[reviewname]
for actor in list(bpy.data.objects):
    if actor.type=='ARMATURE' and set(actor.data.bones.keys())==names:tracks(actor)
assert old_names<={a.name for a in bpy.data.actions}
assert old_civic_actions=={n:action_signature(bpy.data.actions[n]) for n in old_names}
assert old_civic_scenes<=set(bpy.data.scenes.keys())
bpy.context.window.scene=bpy.data.scenes[civic_entry]
bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(civic),compress=True)
(ROOT/'Validation/acceleration_project_preservation.json').write_text(json.dumps(dict(
    master_entry_scene=entry,civic_entry_scene=civic_entry,master_old_actions_preserved=len(proof['old_actions']),
    civic_old_actions_preserved=len(old_civic_actions),civic_old_scenes_preserved=sorted(old_civic_scenes),
    review_scene=reviewname,all_checks_passed=True),indent=2)+'\n')
print('ACCELERATION_SOURCE_PUBLISHED',len(SPECS),'NEW; BOTH PROJECT ENTRY SCENES PRESERVED',flush=True)
