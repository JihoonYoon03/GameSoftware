"""Partial Blender action update, with immutable pre-existing action/mesh/skin checks."""
import ast, bpy, json, sys, hashlib, math
from pathlib import Path
from mathutils import Matrix,Vector
import numpy as np
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import Pose
from acceleration_motion import SPECS,REVISION,pose_for,phase_at,distance_at
master=ROOT/'Source/Erma_Resource_Library.blend'
bpy.ops.wm.open_mainfile(filepath=str(master))
entry=bpy.context.scene.name
rig=bpy.data.objects['HUMAN_7H'];human=rig
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
PROTOTYPES={a['id']:bpy.data.objects.get(a.get('blender_object',a['id'])) for a in catalog['assets']}
MATS={m.name:m for m in bpy.data.materials}
vehicles=json.loads((ROOT/'Documentation/vehicle_assemblies.json').read_text(encoding='utf-8'))
tree=ast.parse((ROOT/'Tools/build_civic_animations.py').read_text(encoding='utf-8'))
nodes=[n for n in tree.body if (isinstance(n,ast.FunctionDef) and n.name=='geometry_signature') or (isinstance(n,ast.ClassDef) and n.name=='Clearance')]
exec(compile(ast.Module(body=nodes,type_ignores=[]),'<existing geometry QA>','exec'))
def signature(a):
    data=[(f.data_path,f.array_index,f.extrapolation,[(list(k.co),list(k.handle_left),list(k.handle_right),k.interpolation) for k in f.keyframe_points]) for f in a.fcurves]
    return hashlib.sha256(json.dumps(data,separators=(',',':')).encode()).hexdigest()
old={a.name:signature(a) for a in bpy.data.actions if a.name not in {'HUMAN_7H__'+n for n in SPECS}}
geometry_before=geometry_signature()
bind_before=[(b.name,b.parent.name if b.parent else None,[list(row) for row in b.matrix_local]) for b in rig.data.bones]
scene=bpy.data.scenes.new('Acceleration_Authoring_Temporary');scene.render.fps=30
scene.collection.objects.link(rig);bpy.context.window.scene=scene
cache={};built=set()
def sample(name,phase):
    if name=='Sprint' and name not in built:
        from acceleration_motion import sprint
        p=sprint(rig,phase);clearance.correct(p);return p
    key=(name,round(phase,9))
    if key in cache:return cache[key]
    a=bpy.data.actions['HUMAN_7H__'+name];rig.animation_data.action=a;rig.animation_data.action_slot=a.slots[0]
    rig.animation_data.use_nla=False;rig.update_tag();scene.frame_set(-1)
    f=a.frame_range[0]+(a.frame_range[1]-a.frame_range[0])*phase
    scene.frame_set(math.floor(f),subframe=f%1);bpy.context.view_layer.update()
    e=rig.evaluated_get(bpy.context.evaluated_depsgraph_get());p=Pose(rig)
    p.basis={b.name:b.matrix_basis.copy() for b in e.pose.bones};cache[key]=p
    return p
clearance=Clearance()
report=dict(revision=REVISION,master_sha256=hashlib.sha256(master.read_bytes()).hexdigest(),entry_scene=entry,
    geometry_before=geometry_before,old_actions=old,clips={},all_checks_passed=False)
trajectories={}
for name in ['Sprint']+[n for n in SPECS if n!='Sprint']:
    info=SPECS[name]
    poses=[];low=999;max_correction=0
    for i in range(info['frames']*2+1):
        t=i/(info['frames']*2);p=pose_for(rig,name,t,sample)
        before={n:p.basis[n].copy() for n in p.basis}
        # The shared correction checks fitted skin, soles, greaves and rigid equipment.
        # Exact endpoints use the existing action pose, which is already validated.
        if 0<t<1 or info['loop']:clearance.correct(p)
        max_correction=max(max_correction,max(abs(p.basis[n][r][c]-before[n][r][c]) for n in p.basis for r in range(4) for c in range(4)))
        low=min(low,clearance.minimum(p));poses.append(p)
    # Ensure the procedural loop matches exactly, including clearance corrections.
    if info['loop']:poses[-1]=poses[0]
    action=bpy.data.actions.get('HUMAN_7H__'+name) or bpy.data.actions.new('HUMAN_7H__'+name)
    action.fcurves.clear();action.use_fake_user=True;action['motion_revision']=REVISION;action['loop']=info['loop']
    rig.animation_data.action=action;rig.animation_data.use_nla=False
    if action.slots:rig.animation_data.action_slot=action.slots[0]
    prev={}
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
    tr=rig.animation_data.nla_tracks.new();tr.name=name
    st=tr.strips.new(name,0,action);st.name=name;tr.mute=True
    endpoints={}
    if not info['loop']:
        for label,p,base,phase in [('entry',poses[0],info['source'],info['entry_phase']),('exit',poses[-1],info['target'],info['exit_phase'])]:
            q=sample(base,phase)
            endpoints[label]=max(abs(p.fk(n)[r][c]-q.fk(n)[r][c]) for n in p.rest for r in range(4) for c in range(4))
    report['clips'][name]={**info,'min_all_fitted_skin_z_m':low,'endpoint_errors_m':endpoints,'max_clearance_basis_correction':max_correction}
    built.add(name)
    trajectories[name]=[dict(seconds=i/60,normalized_phase=i/(info['frames']*2),
        locomotion_phase=phase_at(info,i/(info['frames']*2))%1,
        speed_mps=info['start_speed_mps']+(info['end_speed_mps']-info['start_speed_mps'])*(3*(i/(info['frames']*2))**2-2*(i/(info['frames']*2))**3),
        reference_forward_distance_m=distance_at(info,i/(info['frames']*2))) for i in range(info['frames']*2+1)]
    print('ACCELERATION_BAKED',name,'MIN_Z',round(low,5),'ENDPOINT',endpoints,flush=True)
assert old=={n:signature(bpy.data.actions[n]) for n in old},'pre-existing action modified'
assert geometry_before==geometry_signature(),'mesh or skin changed'
assert bind_before==[(b.name,b.parent.name if b.parent else None,[list(row) for row in b.matrix_local]) for b in rig.data.bones]
assert len(rig.data.bones)==23
report['geometry_after']=geometry_signature();report['old_actions_preserved']=True;report['bind_preserved']=True
assert all(max(c['endpoint_errors_m'].values(),default=0)<3e-5 for c in report['clips'].values())
assert min(c['min_all_fitted_skin_z_m'] for c in report['clips'].values())>=-.005
report['all_checks_passed']=True
(ROOT/'Validation/acceleration_authoring.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
(ROOT/'Documentation/acceleration_trajectories.json').write_text(json.dumps(dict(units='meters_seconds',reference_only=True,
    root_motion=False,actor_authority='game code; these are synchronization reference curves, not forced movement',clips=trajectories),indent=2)+'\n',encoding='utf-8')
rig.animation_data.action=None;Pose(rig).apply();bpy.context.window.scene=bpy.data.scenes[entry]
bpy.data.scenes.remove(scene);bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Validation/Acceleration_Draft.blend'),compress=True)
print('ACCELERATION_DRAFT_SAVED',len(SPECS),flush=True)
