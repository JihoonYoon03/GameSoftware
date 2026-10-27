"""Partial update: animation data and assembled animated exports only.

Open the existing master first. Never executes the full resource-pack builder.
Use --draft for a quick pose-review file before delivering exports.
"""
import ast,hashlib,json,math,sys
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix,Quaternion,Vector

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import *
TAG='civic_motion_v2_directional'
DRAFT='--draft' in sys.argv
bpy.context.preferences.filepaths.save_version=0
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
clips=json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))
vehicles=json.loads((ROOT/'Documentation/vehicle_assemblies.json').read_text(encoding='utf-8'))
human=bpy.data.objects['HUMAN_7H']
scene=bpy.data.scenes['00_Library'];bpy.context.window.scene=scene;scene.render.fps=30
MATS={m.name:m for m in bpy.data.materials}
PROTOTYPES={a['id']:bpy.data.objects.get(a['id']) for a in catalog['assets']}
for n,o in list(PROTOTYPES.items()):
    if o:continue
    choices=[o for o in scene.objects if o.type=='MESH' and o.get('asset_id')==n and not o.name.startswith(('RAW_','Source_'))]
    if choices:PROTOTYPES[n]=min(choices,key=lambda o:len(o.name))
assert all(PROTOTYPES.values()),'missing canonical mesh'
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
helper_names={'clone_actor','studio','ground','text_label'}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in helper_names],type_ignores=[]),'original_studio_helpers','exec'),globals())

def write(path,data):
    path=ROOT/path;path.parent.mkdir(parents=True,exist_ok=True)
    staged=path.with_suffix(path.suffix+'.new');staged.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8');staged.replace(path)

def geometry_signature():
    h=hashlib.sha256()
    for a in sorted(catalog['assets'],key=lambda x:x['id']):
        o=PROTOTYPES[a['id']];h.update(a['id'].encode())
        h.update(np.array([v.co[:] for v in o.data.vertices],dtype=np.float32).tobytes())
        h.update(str([(tuple(p.vertices),p.material_index) for p in o.data.polygons]).encode())
        h.update(str([[tuple((g.group,g.weight)) for g in v.groups] for v in o.data.vertices]).encode())
    return h.hexdigest()

original_geometry=geometry_signature()
bind_signature={o.name:[(b.name,b.parent.name if b.parent else None,list(sum([list(row) for row in b.matrix_local],[]))) for b in o.data.bones]
                for o in [human]+[bpy.data.objects[v['skeleton']] for v in vehicles['types'].values()]}

class Clearance:
    """Evaluate real skin vertices, including recipe equipment fitting offsets."""
    def __init__(self):
        self.names=[b.name for b in human.data.bones];self.indices={n:i for i,n in enumerate(self.names)}
        rows={}
        for a in catalog['assets']:
            if a.get('skeleton')=='HUMAN_7H':rows[(a['id'],(0,0,0))]=None
        for recipe in recipes.values():
            if not isinstance(recipe,dict) or 'body' not in recipe:continue
            for part in recipe['parts']:rows[(part,tuple(recipe.get('offsets',{}).get(part,(0,0,0))))]=None
        groups={};labels={}
        for name,offset in rows:
            ob=PROTOTYPES[name];off=Vector(offset)
            for v in ob.data.vertices:
                ws=tuple((self.indices[ob.vertex_groups[g.group].name],round(g.weight,7)) for g in v.groups if g.weight>1e-7)
                if not ws:continue
                groups.setdefault(ws,[]).append(tuple(v.co+off)+(1,));labels.setdefault(ws,[]).append(name)
        self.groups=[(ws,np.array(points,dtype=np.float64)) for ws,points in groups.items()]
        self.labels=labels;self.offender=None
        self.vertices=sum(len(points) for ws,points in self.groups)
        self.leg_groups={s:[(ws,points) for ws,points in self.groups if all(self.names[i]=='shin.'+s for i,w in ws)] for s in ['L','R']}
    def leg_minimum(self,p,suffix):
        transforms={i:p.deformation(self.names[i]) for ws,points in self.leg_groups[suffix] for i,w in ws}
        return min(float(np.min(points@sum((np.array(transforms[i][2])*w for i,w in ws),np.zeros(4)))) for ws,points in self.leg_groups[suffix])
    def minimum(self,p):
        transforms=[p.deformation(n) for n in self.names];minimum=999
        for ws,points in self.groups:
            row=sum((np.array(transforms[i][2])*weight for i,weight in ws),np.zeros(4))
            values=points@row;low=float(np.min(values))
            if low<minimum:minimum=low;self.offender=self.labels[ws][int(np.argmin(values))]
        return minimum
    def correct(self,p):
        # Repair the actual supporting limb before moving the whole body. Slerping
        # a fall/get-up pose otherwise sweeps the boot under the floor mid-blend.
        for suffix in ['L','R']:
            end='foot.'+suffix;ob=PROTOTYPES['Boot_'+suffix]
            low=min(p.point(end,v.co).z for v in ob.data.vertices)
            if low<.0003:
                u='thigh.'+suffix;l='shin.'+suffix
                hip=p.fk(u).translation;knee=p.fk(l).translation;target=p.fk(end).translation.copy()
                delta=p.fk(end).to_quaternion()@p.rest[end].to_quaternion().inverted()
                pole=knee-(hip+target)*.5;target.z+=.0005-low
                p.chain(u,l,end,target,pole,delta)
            if self.leg_minimum(p,suffix)<.00025:
                u='thigh.'+suffix;l='shin.'+suffix
                hip=p.fk(u).translation;knee=p.fk(l).translation;target=p.fk(end).translation.copy()
                delta=p.fk(end).to_quaternion()@p.rest[end].to_quaternion().inverted()
                pole=knee-(hip+target)*.5
                passed=False
                for rise in [.04,.08,.16,.32,.64,1.28,2.56,5.12]:
                    p.chain(u,l,end,target,pole+Vector((0,0,rise)),delta)
                    if self.leg_minimum(p,suffix)>=.0005:passed=True;break
                if passed:
                    lo=0;hi=rise
                    for iteration in range(12):
                        mid=(lo+hi)*.5;p.chain(u,l,end,target,pole+Vector((0,0,mid)),delta)
                        if self.leg_minimum(p,suffix)>=.0005:hi=mid
                        else:lo=mid
                    p.chain(u,l,end,target,pole+Vector((0,0,hi)),delta)
                else:p.chain(u,l,end,target,pole,delta)
        weapon=PROTOTYPES['Weapon_CoilPistol_35cm']
        low=min(p.point('weapon_socket',v.co).z for v in weapon.data.vertices)
        if low<.015:
            target=p.fk('hand.R').translation.copy();target.z=max(target.z,.22)
            p.chain('upper_arm.R','forearm.R','hand.R',target,(-1,0,.2),rx(-PI/2))
        low=self.minimum(p);lift=max(0,.001-low)
        if lift:p.translate('pelvis',(0,0,lift))
        return p,lift,low

clearance=Clearance();print('CLEARANCE_VERTICES',clearance.vertices,flush=True)
report={'revision':TAG,'geometry_before_sha256':original_geometry,'human':{},'vehicles':{},'clearance_vertices':clearance.vertices}
old_human=[(c['clip'],c['end'],c['loop']) for c in clips['HUMAN_7H'] if c['clip'] not in {n for n,f,l in NEW_HUMAN}]
assert len(old_human)==15,'preserve all original clips'
human_specs=old_human+NEW_HUMAN

def clear_track(rig,name):
    rig.animation_data_create()
    for track in list(rig.animation_data.nla_tracks):
        if track.name==name:rig.animation_data.nla_tracks.remove(track)

def bake(rig,name,frames,loop,function):
    action_name=rig.name+'__'+name
    old=bpy.data.actions.get(action_name)
    if old:
        previous='UpperBodyV1__'+action_name
        if not bpy.data.actions.get(previous):
            saved=old.copy();saved.name=previous;saved.use_fake_user=True
        archive='Baseline__'+action_name
        if not bpy.data.actions.get(archive):
            saved=old.copy();saved.name=archive;saved.use_fake_user=True
        # Keep the Action identity; all existing shared source actors see the revision.
        action=old;action.fcurves.clear()
    else:action=bpy.data.actions.new(action_name)
    action.use_fake_user=True;action['motion_revision']=TAG;action['loop']=loop
    rig.animation_data_create();rig.animation_data.use_nla=False
    rig.animation_data.action=action
    if action.slots:rig.animation_data.action_slot=action.slots[0]
    maximum_lift=0;minimum=999;poses=[];offender=None;worst_frame=0
    for sample in range(frames*2+1):
        f=sample/2
        p=function(rig,name,f/frames)
        if rig==human and name not in ['Reference_A','Turn_Base','Ride_Base','Rider_Brace']:
            p,lift,low=clearance.correct(p)
            if lift>maximum_lift:maximum_lift=lift;offender=clearance.offender;worst_frame=f
            minimum=min(minimum,low+lift)
        poses.append(p)
    temporal_lifts=[0. for p in poses]
    if rig==human and name not in ['Reference_A','Turn_Base','Ride_Base','Rider_Brace']:
        # Validate the exported SLERP between keys as well as the keys themselves.
        # Fast contacts can intersect the floor between two individually valid poses.
        for iteration in range(3):
            additions=[0. for p in poses]
            for i,(a,b) in enumerate(zip(poses,poses[1:])):
                for fraction in [.25,.5,.75]:
                    mid=Pose(rig)
                    for n in mid.basis:
                        la,qa,sa=a.basis[n].decompose();lb,qb,sb=b.basis[n].decompose()
                        mid.basis[n]=Matrix.LocRotScale(la.lerp(lb,fraction),qa.slerp(qb,fraction),sa.lerp(sb,fraction))
                    need=.001-clearance.minimum(mid)
                    if need<=.00005:continue
                    if i==0:additions[i+1]=max(additions[i+1],need/fraction)
                    elif i+1==len(poses)-1:additions[i]=max(additions[i],need/(1-fraction))
                    else:
                        additions[i]=max(additions[i],need);additions[i+1]=max(additions[i+1],need)
            if max(additions)<.00005:break
            for i,lift in enumerate(additions):
                if lift:poses[i].translate('pelvis',(0,0,lift));temporal_lifts[i]+=lift
    for sample,p in enumerate(poses):
        f=sample/2
        p.apply()
        for bone in rig.pose.bones:
            if f:
                previous=prev[bone.name]
                if bone.rotation_quaternion.dot(previous)<0:bone.rotation_quaternion.negate()
            bone.keyframe_insert('rotation_quaternion',frame=f);bone.keyframe_insert('location',frame=f)
        prev={b.name:b.rotation_quaternion.copy() for b in rig.pose.bones}
    for fc in action.fcurves:
        for key in fc.keyframe_points:key.interpolation='LINEAR'
    rig.animation_data.action=None
    clear_track(rig,name)
    track=rig.animation_data.nla_tracks.new();track.name=name
    strip=track.strips.new(name,0,action);strip.name=name;track.mute=True
    Pose(rig).apply()
    data={'frames':frames,'loop':loop,'max_clearance_correction_m':maximum_lift,'max_temporal_clearance_lift_m':max(temporal_lifts),'clearance_limiting_part':offender,'worst_frame':worst_frame,'min_source_vertex_z_m':minimum if minimum<900 else None}
    if loop:
        data['loop_max_matrix_error']=max(abs(poses[0].fk(n)[i][j]-poses[-1].fk(n)[i][j]) for n in poses[0].rest for i in range(4) for j in range(4))
        assert data['loop_max_matrix_error']<2e-5,(name,'loop seam',data)
    if name not in ['Turn_Left_90','Turn_Right_90']:
        assert max((p.fk('root').translation-poses[0].fk('root').translation).length for p in poses)<1e-6
    print('BAKED',rig.name,name,'LIFT',round(maximum_lift,4),flush=True)
    return action,data

for name,frames,loop in human_specs:
    a,result=bake(human,name,frames,loop,human_pose);report['human'][name]=result
for kind,info in vehicles['types'].items():
    rig=bpy.data.objects[info['skeleton']];report['vehicles'][kind]={}
    specs=NEW_VEHICLE+([('Bike_Impact',15,False)] if kind=='JetBike' else [])
    for name,frames,loop in specs:
        a,result=bake(rig,name,frames,loop,vehicle_pose);report['vehicles'][kind][name]=result

# Existing scene actors share the original Actions. Add the new NLA tracks to
# every compatible humanoid/vehicle copy without replacing any mesh or armature.
for rig in list(bpy.data.objects):
    if rig.type!='ARMATURE':continue
    signature=set(b.name for b in rig.data.bones)
    canonical=human if signature==set(b.name for b in human.data.bones) else None
    if not canonical and rig.get('vehicle_type') in vehicles['types']:
        canonical=bpy.data.objects['VEHICLE_'+rig['vehicle_type']]
    if canonical and rig!=canonical:
        for original in canonical.animation_data.nla_tracks:
            clear_track(rig,original.name)
            tr=rig.animation_data.nla_tracks.new();tr.name=original.name
            st=tr.strips.new(tr.name,0,original.strips[0].action);st.name=tr.name;tr.mute=True

def entry(name,frames,loop,human_clip=True):
    d=dict(clip=name,action=('HUMAN_7H' if human_clip else '')+'__'+name,fps=30,start=0,end=frames,
           duration_seconds=frames/30,loop=loop,root_translation='in_place',root_yaw_degrees=0,
           blend_in_seconds=.04 if name.startswith('Hit') else .08,
           blend_out_seconds=.06 if name.startswith('Hit') else .12,
           support='see civic_animation_contract.json',events=[],revision=TAG,authoring_fps=30,delivery_sampling_fps=60)
    def event(phase,event,**extra):d['events'].append(dict(phase=phase,seconds=round(frames/30*phase,6),event=event,**extra))
    if name.startswith('Hit_'):
        d.update(layer='human_upper_body',reference='Aim_Base',blend_mode='masked_override',entry_pose='Aim_Base',exit_pose='Aim_Base')
        event(0,'impact_visual_only')
    elif name.startswith(('Aim_Walk_','Aim_Run_')):
        direction=name.split('_',2)[2];angle=math.radians(DIRECTION_ANGLES[direction]);fast=name.startswith('Aim_Run_')
        d.update(layer='full_body',entry_pose=name+':0',exit_pose=name+':0',phase_sync_group='aim_locomotion',
                 facing='fixed -Y; game owns actor yaw',travel_direction_local=[round(math.sin(angle),8),round(-math.cos(angle),8),0],
                 direction_degrees=DIRECTION_ANGLES[direction],speed_mps=aim_speed(direction,fast),
                 contacts={'left':[[0,.4 if fast else .6]],'right':[[.5,.9]] if fast else [[0,.1],[.5,1]]},
                 upper_body_reference='Aim_Base',support='uncrossed feet; phase zero is left contact in all directions')
        event(0,'left_foot_contact');event(.5,'right_foot_contact')
    elif name.startswith(('Knockdown_','Death_')) and not name.endswith('Pose'):
        direction=name.split('_')[1];down='Downed_Loop' if direction=='Front' else 'Downed_'+direction+'_Loop'
        d.update(layer='full_body',entry_pose='Idle:0',exit_pose=name+'_Pose' if name.startswith('Death') else down+':0',hold_last_frame=True)
        event(.66,'body_contact_visual_only')
        if name.startswith('Death'):event(1,'death_pose_hold',auto_getup=False)
        else:event(1,'downed_pose_ready')
    elif name.startswith('GetUp_') or name=='Medical_GetUp':
        direction=name.split('_')[1] if name.startswith('GetUp_') else 'Front'
        d.update(entry_pose='Downed_Loop:0' if direction=='Front' else 'Downed_'+direction+'_Loop:0',exit_pose='Idle:0',input_lock_until_event='fully_upright')
        event(.26,'left_hand_support');event(.53,'left_foot_planted');event(.78,'knee_support_released');event(1,'fully_upright')
    elif name.startswith('Downed_'):
        d.update(entry_pose=name+':0',exit_pose=name+':0',support='side torso, hip, bent legs; weapon clear of floor')
    elif name.endswith('_Pose'):
        d.update(entry_pose=name.removesuffix('_Pose')+':1',hold_last_frame=True,auto_getup=False)
    elif name=='Security_StunFire':
        d.update(layer='human_upper_body',reference='Aim_Base',blend_mode='masked_override',entry_pose='Aim_Base',exit_pose='Aim_Base')
        event(.08,'stun_projectile_trigger',socket='muzzle',attachment='Weapon_CoilPistol_35cm')
    elif name=='Flee_Start':d.update(entry_pose='Idle:0',exit_pose='Flee_Run:0',heading='actor supplied escape heading, gaze forward')
    elif name=='Flee_Stop':d.update(entry_pose='Flee_Run:0',exit_pose='Idle:0',entry_phase=0)
    elif name=='Flee_Run':d.update(entry_pose='Run:0',exit_pose='Run:0',support='left 0-.4, right .5-.9, flight .4-.5/.9-1')
    elif name in ['Ride_Base','Rider_Brace']:
        d.update(entry_pose='Ride_Base',exit_pose='Ride_Base',layer='full_body',support='seat, handlebar L/R, footrest L/R',legacy_game_Ride='preserved outside source; no Runtime edits')
        event(0,'riding_weapon_visibility',visible=False,reason='both hands on handlebars; holster in game')
    elif name=='Medical_Stabilized':d.update(entry_pose='Downed_Loop:0',exit_pose='Downed_Loop:0')
    return d

original_meta={c['clip']:c for c in clips['HUMAN_7H']}
clips['HUMAN_7H']=[]
for n,f,l in human_specs:
    e=entry(n,f,l)
    if n in original_meta:
        e={**original_meta[n],**{k:v for k,v in e.items() if k in ['revision']}}
        e['motion_refinement']='weight transfer, parent-aware IK, relaxed arms, matched transitions'
        e['authoring_fps']=30;e['delivery_sampling_fps']=60
    clips['HUMAN_7H'].append(e)
for kind,info in vehicles['types'].items():
    name=info['skeleton'];old=[c for c in clips[name] if c['clip'] not in report['vehicles'][kind]]
    for n,r in report['vehicles'][kind].items():
        e=entry(n,r['frames'],False,False);e['action']=name+'__'+n;e['root_yaw_degrees']=0
        e.update(layer='field_suspension',support='fixed chassis, driver/passenger sockets and collision hull',entry_pose='Vehicle_Reference',exit_pose=n+':1' if n=='Vehicle_Disabled' else 'Vehicle_Reference')
        if n=='Vehicle_Disabled':
            e['hold_last_frame']=True;e['events']=[dict(phase=0,seconds=0,event='power_cut',emission_multiplier=0.12),dict(phase=.15,seconds=.12,event='spark_billboard',socket='engine.L'),dict(phase=1,seconds=.8,event='wreck_hold',collision_remains_solid=True),dict(phase=1,seconds=.8,event='tow_cleanup_eligible_when_offscreen',policy='game decides delay; opacity fade only')]
        else:e['events']=[dict(phase=.20,seconds=.10,event='impact_spark_billboard',socket='frontpod.L')]
        old.append(e)
    clips[name]=old

def export(file,objects,rig):
    bpy.context.window.scene=scene
    for o in scene.objects:o.select_set(False)
    for o in objects:o.select_set(True)
    rig.select_set(True);bpy.context.view_layer.objects.active=rig
    rig.animation_data.action=None;rig.animation_data.use_nla=True;Pose(rig).apply()
    for track in rig.animation_data.nla_tracks:
        track.mute=False
        for strip in track.strips:strip.scale=2
    scene.render.fps=60
    scene.frame_set(0)
    path=ROOT/file;staged=path.with_name(path.stem+'.staging.glb')
    bpy.ops.export_scene.gltf(filepath=str(staged),export_format='GLB',use_selection=True,use_active_scene=True,
        export_animations=True,export_animation_mode='NLA_TRACKS',export_nla_strips=True,
        export_force_sampling=True,export_frame_range=False,export_frame_step=1,
        export_skins=True,export_def_bones=False,export_yup=True,export_extras=True,
        export_cameras=False,export_lights=False,export_materials='EXPORT',export_apply=False,
        export_texcoords=True,export_normals=True,export_tangents=True,export_anim_slide_to_zero=True,export_optimize_animation_size=True)
    staged.replace(path)
    for track in rig.animation_data.nla_tracks:
        track.mute=True
        for strip in track.strips:strip.scale=1
    scene.render.fps=30
    Pose(rig).apply()

if not DRAFT:
    for e in catalog['exports']:
        file=e['file'].replace('\\','/')
        if '--characters-only' in sys.argv and e['type']!='character':continue
        if '--export-one' in sys.argv and Path(file).stem!=sys.argv[sys.argv.index('--export-one')+1]:continue
        if '--export-rest' in sys.argv and file=='Export/Characters/Player_Explorer.glb':continue
        if e['type']=='character':
            name=Path(file).stem;info=recipes[name]
            col=bpy.data.collections.new('Civic_Export_Temporary');scene.collection.children.link(col)
            rig,objects=clone_actor(name+'_Civic',human,info['parts'],col,skin=info.get('skin'),part_offsets=info.get('offsets'))
            for original in human.animation_data.nla_tracks:
                tr=rig.animation_data.nla_tracks.new();tr.name=original.name
                st=tr.strips.new(tr.name,0,original.strips[0].action);st.name=tr.name;tr.mute=True
            export(file,objects,rig);e['clips']=len(human_specs)
            for o in objects+[rig]:bpy.data.objects.remove(o,do_unlink=True)
            bpy.data.collections.remove(col)
        elif file.startswith('Export/Vehicles/') and '/Parts/' not in file:
            kind=Path(file).stem.removesuffix('_Variant');info=vehicles['types'][kind]
            export(file,[PROTOTYPES[n] for n in e['mesh_parts']],bpy.data.objects[info['skeleton']]);e['clips']=len(clips[info['skeleton']])

def review_scene():
    name='37_Civic_Animation_Review'
    if bpy.data.scenes.get(name):
        old=bpy.data.scenes[name]
        for o in list(old.objects):
            if len(o.users_scene)==1:bpy.data.objects.remove(o,do_unlink=True)
        bpy.data.scenes.remove(old)
    s,c=studio(name,(3.6,-6,3.2),(0,0,.85),3.1,(800,800));s.cycles.samples=10
    info=recipes['Player_Explorer']
    r,obs=clone_actor('Civic_Review',human,info['parts'],c,skin=info.get('skin'),part_offsets=info.get('offsets'))
    r.animation_data.action=bpy.data.actions['HUMAN_7H__Aim_Walk_Left'];r.animation_data.action_slot=r.animation_data.action.slots[0];r.animation_data.use_nla=False
    ground(c,size=8,z=-.008);s.frame_start=0;s.frame_end=30;s.frame_set(7)
    s['motion_revision']=TAG;s['instructions']='Select Civic_Review_Rig, Action Editor: HUMAN_7H__*; 30 fps; metadata in civic_animation_contract.json'
    for area in bpy.context.screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_distance=3.3
    return s

review=review_scene()
assert original_geometry==geometry_signature(),'mesh or weights changed during animation authoring'
for name,signature in bind_signature.items():
    ob=bpy.data.objects[name]
    assert signature==[(b.name,b.parent.name if b.parent else None,list(sum([list(row) for row in b.matrix_local],[]))) for b in ob.data.bones]
report['geometry_after_sha256']=original_geometry;report['bind_poses_preserved']=True
write('Validation/civic_animation_authoring.json',report)
if not DRAFT:
    write('Documentation/animation_clips.json',clips);write('Documentation/asset_catalog.json',catalog)
    bpy.context.window.scene=bpy.data.scenes['16_Transit_Connected_Example']
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'),compress=True)
else:
    bpy.context.window.scene=review
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source/Civic_Animation_Draft.blend'),compress=True)
print('CIVIC_ANIMATION_BUILD_COMPLETE','DRAFT' if DRAFT else 'DELIVERY',len(human_specs),'HUMAN CLIPS',flush=True)
