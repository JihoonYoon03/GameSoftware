"""Publish a 2D velocity blend mesh without changing game code or Runtime."""
import json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
path=ROOT/'Documentation/animation_clips.json';meta=json.loads(path.read_text(encoding='utf-8'))
clips={c['clip']:c for c in meta['HUMAN_7H']};directions=['Forward','ForwardLeft','Left','BackLeft','Back','BackRight','Right','ForwardRight']
samples=[{'clip':'Aim_Base','velocity_mps':[0,0],'phase':'constant'}]
for speed in ['Walk','Run']:
    for d in directions:
        c=clips['Aim_'+speed+'_'+d];v=c['travel_direction_local'];s=c['speed_mps']
        samples.append(dict(clip=c['clip'],velocity_mps=[round(v[0]*s,8),round(-v[1]*s,8)],cycle_seconds=c['duration_seconds'],phase_zero='left contact'))
triangles=[]
for i in range(8):
    j=(i+1)%8;wi,wj=1+i,1+j;ri,rj=9+i,9+j
    triangles.extend([[0,wi,wj],[wi,ri,rj],[wi,rj,wj]])
data=dict(revision='aim_directional_v1',date='2026-10-27',skeleton='HUMAN_7H',bones=23,
          clip_count=16,root_translation='in_place',root_yaw_degrees=0,
          axes={'blend_x':'character local +X = left','blend_y':'character local -Y = forward','input':'world velocity rotated into the fixed aim-facing actor frame; meters/second'},
          samples=samples,triangles=triangles,
          evaluation=['Locate the velocity in a blend triangle; use non-negative barycentric weights summing to one.',
                      'Evaluate all moving clips at the same normalized phase; never reset phase when direction or speed changes.',
                      'Blend bone-local translations linearly and rotations by hemisphere-correct quaternion interpolation.',
                      'Advance phase by dt times the weighted cycle frequency, normalized over moving samples; hold the last phase at zero speed.',
                      'Keep actor facing on the aim target; apply translation through game physics. Clamp speeds outside the outer run polygon or scale playback deliberately.'],
          locomotion_mask_reference='animation_blending.json:masks.human_lower_body',aim_mask_reference='animation_blending.json:masks.human_upper_body',
          source_reference='Aim_Base',loop=True,game_integration_included=False,
          blend_floor_correction={'stage':'after local-transform blending, before skinning','sole_bind_points':{'L':[[.115,-.195,0],[.115,.065,0]],'R':[[-.115,-.195,0],[-.115,.065,0]]},
                                  'formula':'correction_z = max(0, ground_z + 0.001m - min(transformed_sole_points.z)); add to local pelvis translation, leave actor/root/collision unchanged',
                                  'reason':'FK rotation interpolation can lower the sole even when each source clip has planted feet','measured_report':'../Validation/directional_motion_report.json'},
          reference_method='Hand-authored on the original rig after viewing Mixamo public previews; no downloaded motion data or character meshes imported.',
          references=[{'url':'https://www.mixamo.com/#/?page=1&query=walking&type=Motion%2CMotionPack','observed':['Walking — Walking While Aiming Rifle','Walking Backwards — Walking Backwards While Aiming Rifle'],'used_for':'stable sight line, shoulder/pelvis separation, cautious backward steps'},
                      {'url':'https://www.mixamo.com/#/?page=1&query=strafe&type=Motion%2CMotionPack','observed':['Pistol Strafe — Strafe Left With An Aimed Pistol'],'used_for':'elbows below wrists, torso facing the target, uncrossed lateral steps'},
                      {'url':'https://www.mixamo.com/#/?page=1&query=running&type=Motion%2CMotionPack','observed':['Unarmed Run Forward'],'used_for':'sagittal arm swing, bent elbows and contralateral timing'}])
(ROOT/'Documentation/aim_locomotion_blending.json').write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
path=ROOT/'Documentation/animation_blending.json';blend=json.loads(path.read_text(encoding='utf-8'))
blend['locomotion']['aim_directional']={'contract':'aim_locomotion_blending.json','samples':16,'idle':'Aim_Base','facing':'fixed aim facing','phase_reset_on_direction_change':False}
path.write_text(json.dumps(blend,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
path=ROOT/'Documentation/civic_animation_contract.json';contract=json.loads(path.read_text(encoding='utf-8'))
contract['aim_directional']={'contract':'aim_locomotion_blending.json','new_clips':16,'walk_directions':directions,'run_directions':directions,'source_scene':'42_Aim_Directional_Review','runtime_implementation_included':False}
contract['sources'].extend(data['references']);contract['revision']='civic_motion_v2_directional'
path.write_text(json.dumps(contract,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print('DIRECTIONAL_BLEND_CONTRACT_WRITTEN',len(samples),len(triangles),flush=True)
