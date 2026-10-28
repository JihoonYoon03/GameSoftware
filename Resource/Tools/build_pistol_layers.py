"""Bake only four requested layers; keep every pre-existing action and mesh."""
import bpy,sys,json,hashlib,ast
from pathlib import Path
from mathutils import Matrix,Vector
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import Pose
from pistol_layer_motion import SPECS,LOWER,pistol_pose
rig=bpy.data.objects['HUMAN_7H'];meta=json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))
old_names=[c['clip'] for c in meta['HUMAN_7H'] if c['clip'] not in SPECS]
def action_signature(a):
    data=[(fc.data_path,fc.array_index,fc.extrapolation,[(list(k.co),list(k.handle_left),list(k.handle_right),k.interpolation) for k in fc.keyframe_points]) for fc in a.fcurves]
    return hashlib.sha256(json.dumps(data,separators=(',',':')).encode()).hexdigest()
before={n:action_signature(bpy.data.actions['HUMAN_7H__'+n]) for n in old_names}
entry=bpy.context.scene.name;scene=bpy.data.scenes.new('Pistol_Authoring_Temporary');scene.collection.objects.link(rig);scene.render.fps=30;bpy.context.window.scene=scene
report={'revision':'pistol_layers_v1','request':'ANIMATION_BLEND_REQUEST.md','master_sha256':hashlib.sha256((ROOT/'Source/Erma_Resource_Library.blend').read_bytes()).hexdigest(),'old_actions':before,'clips':{}}
def sample(name,phase):
    a=bpy.data.actions['HUMAN_7H__'+name];rig.animation_data.action=a;rig.animation_data.action_slot=a.slots[0];rig.animation_data.use_nla=False;rig.update_tag()
    scene.frame_set(-1);f=a.frame_range[0]+(a.frame_range[1]-a.frame_range[0])*phase;scene.frame_set(int(f),subframe=f%1);bpy.context.view_layer.update()
    e=rig.evaluated_get(bpy.context.evaluated_depsgraph_get());p=Pose(rig);p.basis={b.name:b.matrix_basis.copy() for b in e.pose.bones};return p
for name,(base_name,end,loop) in SPECS.items():
    poses=[];lower_error=0
    for i in range(end*2+1):
        t=i/(end*2);base=sample(base_name,0 if name=='Reload_Pistol' else t);p=pistol_pose(base,name,t)
        lower_error=max(lower_error,max(abs(p.basis[n][r][c]-base.basis[n][r][c]) for n in LOWER for r in range(4) for c in range(4)))
        poses.append(p)
    assert lower_error==0
    action=bpy.data.actions.get('HUMAN_7H__'+name) or bpy.data.actions.new('HUMAN_7H__'+name)
    action.fcurves.clear();action.use_fake_user=True;action['motion_revision']='pistol_layers_v1';action['loop']=loop
    rig.animation_data.action=action;rig.animation_data.use_nla=False;prev={}
    if action.slots:rig.animation_data.action_slot=action.slots[0]
    for i,p in enumerate(poses):
        p.apply()
        for b in rig.pose.bones:
            if b.name in prev and b.rotation_quaternion.dot(prev[b.name])<0:b.rotation_quaternion.negate()
            b.keyframe_insert('rotation_quaternion',frame=i/2);b.keyframe_insert('location',frame=i/2)
        prev={b.name:b.rotation_quaternion.copy() for b in rig.pose.bones}
    for fc in action.fcurves:
        for k in fc.keyframe_points:k.interpolation='LINEAR'
    rig.animation_data.action=None
    for tr in list(rig.animation_data.nla_tracks):
        if tr.name==name:rig.animation_data.nla_tracks.remove(tr)
    tr=rig.animation_data.nla_tracks.new();tr.name=name;strip=tr.strips.new(name,0,action);strip.name=name;tr.mute=True
    seam=max(abs(poses[0].fk(n)[r][c]-poses[-1].fk(n)[r][c]) for n in poses[0].rest for r in range(4) for c in range(4))
    if loop:assert seam<2e-5,(name,seam)
    report['clips'][name]={'frames':end,'loop':loop,'lower_body_matrix_error':lower_error,'loop_error':seam if loop else None}
    print('PISTOL_LAYER_BAKED',name,end,flush=True)
assert before=={n:action_signature(bpy.data.actions['HUMAN_7H__'+n]) for n in old_names}
rig.animation_data.action=None;Pose(rig).apply();bpy.context.window.scene=bpy.data.scenes[entry];bpy.data.scenes.remove(scene)
report['all_existing_actions_preserved']=True
(ROOT/'Validation/pistol_layer_authoring.json').write_text(json.dumps(report,indent=2)+'\n')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Validation/Pistol_Layers_Draft.blend'),compress=True)
print('PISTOL_LAYER_DRAFT_SAVED',len(old_names),'EXISTING +',len(SPECS),'NEW',flush=True)
