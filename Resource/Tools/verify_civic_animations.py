"""Actual GLB re-import, skin, contact, transition and physics-separation QA."""
import json,math,sys
from pathlib import Path
import bpy
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1]
meta=json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
author=json.loads((ROOT/'Validation/civic_animation_authoring.json').read_text(encoding='utf-8'))
report={'characters':{},'vehicles':{},'transitions':{},'contact_checks':{},'penetrations':[],'errors':[]}
assert author['geometry_before_sha256']==author['geometry_after_sha256']
assert author['bind_poses_preserved']

def bind(scene,rig,action,phase):
    changed=rig.animation_data.action!=action
    rig.animation_data.action=action
    if action.slots:rig.animation_data.action_slot=action.slots[0]
    rig.animation_data.use_nla=False
    start,end=action.frame_range;f=start+(end-start)*phase
    if changed:rig.update_tag();scene.frame_set(-1)
    scene.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update()

def points(objects,trees=False):
    result=[];byid={};dg=bpy.context.evaluated_depsgraph_get()
    for ob in objects:
        eo=ob.evaluated_get(dg);mesh=eo.to_mesh();p=[eo.matrix_world@v.co for v in mesh.vertices]
        result.extend(p)
        if trees:
            name=ob.get('asset_id',ob.name)
            byid[name]=BVHTree.FromPolygons(p,[poly.vertices[:] for poly in mesh.polygons])
        eo.to_mesh_clear()
    return result,byid

def pose(rig):return {b.name:b.matrix.copy() for b in rig.pose.bones}
def pose_error(a,b):return max(abs(a[n][i][j]-b[n][i][j]) for n in a for i in range(4) for j in range(4))

def load(file):
    bpy.ops.wm.read_factory_settings(use_empty=True);scene=bpy.context.scene;scene.render.fps=30
    bpy.ops.import_scene.gltf(filepath=str(ROOT/file))
    rigs=[o for o in scene.objects if o.type=='ARMATURE'];assert len(rigs)==1
    rig=rigs[0]
    for t in rig.animation_data.nla_tracks:t.mute=True
    rig.animation_data.use_nla=False
    return scene,rig,[o for o in scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)],{a.name.split('.')[0]:a for a in bpy.data.actions}

def compare(scene,rig,actions,a,pa,b,pb):
    bind(scene,rig,actions[a],pa);first=pose(rig)
    bind(scene,rig,actions[b],pb);return pose_error(first,pose(rig))

for name,info in recipes.items():
    if not isinstance(info,dict) or 'body' not in info:continue
    if '--only' in sys.argv and name!=sys.argv[sys.argv.index('--only')+1]:continue
    scene,rig,meshes,actions=load('Export/Characters/'+name+'.glb')
    assert len(rig.data.bones)==23
    assert {o.get('asset_id') for o in meshes}==set(info['parts']),[(o.name,list(o.keys())) for o in meshes if o.get('asset_id') not in info['parts']]
    assert set(actions)=={c['clip'] for c in meta['HUMAN_7H']},(name,set(actions))
    result={'bones':23,'mesh_parts':len(meshes),'clips':{},'equipment_head_intersections':[]}
    for c in meta['HUMAN_7H']:
        clip=c['clip'];action=actions[clip];duration=(action.frame_range[1]-action.frame_range[0])/30
        assert abs(duration-c['duration_seconds'])<1e-5,(clip,duration)
        low=999;motion=0;root_error=0;seam=0;reference=None
        # Every source frame, plus midframes for fast falls and recoveries.
        phases=[i/c['end'] for i in range(c['end']+1)]
        if clip.startswith(('Knockdown','GetUp','Death','Jump')):
            phases=sorted(set(phases+[(i+offset)/c['end'] for i in range(c['end']) for offset in [.25,.5,.75]]))
        for phase in phases:
            bind(scene,rig,action,phase)
            test_collision=clip.startswith(('Knockdown','GetUp','Downed','Hit_')) and (abs(phase*8-round(phase*8))<.05 or phase in [0,1])
            vertices,trees=points(meshes,trees=test_collision)
            assert all(math.isfinite(x) for v in vertices for x in v)
            z=min(v.z for v in vertices);low=min(low,z)
            if reference is None:reference=vertices;first_pose=pose(rig);root=rig.pose.bones['root'].matrix.translation.copy()
            motion=max(motion,max((a-b).length for a,b in zip(reference,vertices)))
            root_error=max(root_error,(rig.pose.bones['root'].matrix.translation-root).length)
            if test_collision:
                weapon=trees.get('Weapon_CoilPistol_35cm')
                if weapon:
                    for part,tree in trees.items():
                        if part.startswith(('Head_','Hair_','Helmet_','Hat_')) and weapon.overlap(tree):
                            result['equipment_head_intersections'].append([clip,round(phase,4),'weapon',part])
                packs=[n for n in trees if any(s in n for s in ['TwinTank','PowerRig','Combat_Reactor','RescuePack','SampleRack','CompactPack','Rucksack'])]
                for pack in packs:
                    for part,tree in trees.items():
                        if part.startswith(('Head_','Hair_','Helmet_','Hat_')) and trees[pack].overlap(tree):
                            result['equipment_head_intersections'].append([clip,round(phase,4),pack,part])
            if phase==1:seam=pose_error(first_pose,pose(rig))
        assert low>=-.005,(name,clip,'floor penetration',low)
        assert root_error<2e-5,(name,clip,'root moves',root_error)
        if c['loop']:assert seam<2e-4,(name,clip,'loop seam',seam)
        if clip not in ['Reference_A','Turn_Base','Aim_Base','Ride_Base','Death_Front_Pose','Death_Back_Pose']:
            assert motion>1e-4,(name,clip,'frozen')
        result['clips'][clip]={'duration_seconds':duration,'min_z_m':low,'max_vertex_motion_m':motion,'root_translation_error_m':root_error,'loop_seam':seam if c['loop'] else None}
    transitions=[('Walk_To_Run',0,'Walk',0),('Walk_To_Run',1,'Run',0),
                 ('Jump_Start',1,'Jump_Air',0),('Jump_Air',1,'Jump_Land',0),
                 ('Attack_Fire',0,'Aim_Base',0),('Attack_Fire',1,'Aim_Base',0),
                 ('Flee_Start',0,'Idle',0),('Flee_Start',1,'Flee_Run',0),
                 ('Flee_Stop',0,'Flee_Run',0),('Flee_Stop',1,'Idle',0),
                 ('Rider_Brace',0,'Ride_Base',0),('Rider_Brace',1,'Ride_Base',0),
                 ('Medical_Stabilized',1,'Medical_GetUp',0)]
    for d in ['Front','Back','Left','Right']:
        down='Downed_Loop' if d=='Front' else 'Downed_'+d+'_Loop'
        transitions += [('Knockdown_'+d,1,down,0),(down,0,'GetUp_'+d,0),('GetUp_'+d,1,'Idle',0),
                        ('Hit_'+d,0,'Aim_Base',0),('Hit_'+d,1,'Aim_Base',0)]
    for d in ['Front','Back']:transitions.append(('Death_'+d,1,'Death_'+d+'_Pose',0))
    errors={f'{a}:{pa}->{b}:{pb}':compare(scene,rig,actions,a,pa,b,pb) for a,pa,b,pb in transitions}
    assert max(errors.values())<3e-4,(name,'transition mismatch',errors)
    assert not result['equipment_head_intersections'],(name,result['equipment_head_intersections'])
    report['transitions'][name]=errors;report['characters'][name]=result
    print('CIVIC_CHARACTER_VERIFIED',name,len(actions),'CLIPS',flush=True)

for kind in ([] if '--only' in sys.argv else ['Coupe','Shuttle','Hauler','Patrol','JetBike']):
    for suffix in ['', '_Variant']:
        scene,rig,meshes,actions=load('Export/Vehicles/'+kind+suffix+'.glb')
        expected={c['clip'] for c in meta['VEHICLE_'+kind]};assert set(actions)==expected
        result={}
        for clip in ['Vehicle_Impact_Front','Vehicle_Impact_Rear','Vehicle_Impact_Side','Vehicle_Disabled']+(['Bike_Impact'] if kind=='JetBike' else []):
            initial=None;maxmotion=0;fixed=None;error=0
            for i in range(16):
                bind(scene,rig,actions[clip],i/15);vertices,_=points(meshes)
                current={b:rig.pose.bones[b].matrix.copy() for b in ['root','chassis','driver_socket','passenger_socket']}
                if initial is None:initial=vertices;fixed=current
                error=max(error,pose_error(fixed,current));maxmotion=max(maxmotion,max((a-b).length for a,b in zip(initial,vertices)))
            assert error<2e-5,(kind,clip,'collision/seat moved',error)
            assert maxmotion>1e-4,(kind,clip,'frozen')
            result[clip]={'fixed_chassis_and_seat_error':error,'max_part_motion_m':maxmotion}
        report['vehicles'][kind+suffix]=result
        print('CIVIC_VEHICLE_VERIFIED',kind+suffix,flush=True)

report['all_checks_passed']=True
(ROOT/'Validation/civic_animation_roundtrip.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('CIVIC_ROUNDTRIP_COMPLETE',len(report['characters']),len(report['vehicles']),flush=True)
