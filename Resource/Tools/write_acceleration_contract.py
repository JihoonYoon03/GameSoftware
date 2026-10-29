"""Append delivery metadata without altering earlier clip contracts or the request."""
import json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
proof=json.loads((ROOT/'Validation/acceleration_authoring.json').read_text())
specs=proof['clips']
def read(p):return json.loads((ROOT/p).read_text(encoding='utf-8'))
def write(p,x):(ROOT/p).write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
meta=read('Documentation/animation_clips.json');old=[c for c in meta['HUMAN_7H'] if c['clip'] not in specs]
for n,s in specs.items():
    a,b=s['source'],s['target'];events=[]
    if s['loop']:
        events=[dict(phase=0,seconds=0,event='left_heel_contact'),dict(phase=.5,seconds=.25,event='right_heel_contact')]
    else:
        events=[dict(phase=1,seconds=s['duration_seconds'],event='locomotion_handoff',target_clip=b,
                     target_normalized_phase=s['exit_phase'],visual_only=True)]
    old.append(dict(clip=n,action='HUMAN_7H__'+n,fps=30,start=0,end=s['frames'],
        duration_seconds=s['duration_seconds'],loop=s['loop'],root_translation='in_place',root_yaw_degrees=0,
        authoring_fps=30,delivery_sampling_fps=60,revision='acceleration_v1',layer='full_body_locomotion',
        armed_lower_body_mask='human_lower_body',armed_upper_body='existing Pistol_Carry / Aim / Reload layers',
        entry_pose=a+':'+str(s['entry_phase']),exit_pose=b+':'+str(s['exit_phase']),
        entry_phase=s['entry_phase'],exit_phase=s['exit_phase'],blend_in_seconds=.08,blend_out_seconds=.10,
        support='entry '+s['entry_contact']+' contact; sampled support windows in acceleration_foot_contacts.json',
        reference_speed_start_mps=s['start_speed_mps'],reference_speed_end_mps=s['end_speed_mps'],
        phase_sync_group='human_locomotion',phase_reset_on_exit=False,events=events))
meta['HUMAN_7H']=old;write('Documentation/animation_clips.json',meta)
blend=read('Documentation/animation_blending.json')
blend['acceleration']={'contract':'acceleration_blending.json','new_clips':list(specs),
    'preserve_existing_walk_to_run':True,'preserve_existing_aim_2d':True,'preserve_phase_on_handoff':True}
write('Documentation/animation_blending.json',blend)
contract=dict(schema='erma_acceleration_v1',date='2026-10-29',request='../ANIMATION_BLEND_REQUEST.md',
    skeleton='HUMAN_7H',bones=23,previous_human_clips_preserved=63,total_human_clips=len(old),new_clips=list(specs),
    axes=dict(forward='-Y',left='+X',up='+Z'),units='meters_seconds',fps_authoring=30,sampling_hz_delivery=60,
    speeds_mps={'Walk':1.08,'Run':2.46,'Sprint':4.2},speeds_world_units_per_second={'Walk':.54,'Run':1.23,'Sprint':2.1},
    sprint=dict(period_seconds=.5,stance_fraction=.32,left_contact_phase=0,right_contact_phase=.5,
        intended_mode='forward unarmed or lowered-carry sprint; aiming uses the unchanged direction-specific samples'),
    motion_authority=dict(root_translation=False,root_yaw=False,actor_movement='game only',collision='game only',
        reference_speed_curves='acceleration_trajectories.json',support_samples='acceleration_foot_contacts.json',
        speed_curve_profile='v(t)=v0+(v1-v0)*(3u^2-2u^3); integral is reference only',
        warning='The in-place transition does not impose braking distance. Actor acceleration, collision and terrain adaptation must agree with authored timing.'),
    selection=dict(entry_contact_L_phase=0,entry_contact_R_phase=.5,normalized_contact_distance='choose the closest 0 or 0.5 modulo 1',
        maximum_contact_wait_seconds=.10,immediate_response='do not delay collision or input; crossfade 0.08s from the evaluated pose when off-contact',
        phase_on_exit='continue the target loop at exit_phase; never reset to zero',
        switching_mid_transition='evaluate current pose and speed; blend to the appropriate new clip, never snap to its first key'),
    layering=dict(unarmed='full body; hide the weapon mesh',armed='new lower-body mask + existing speed-blended Pistol_Carry upper body',
        carry_speed_samples=['Pistol_Carry_Idle','Pistol_Carry_Walk','Pistol_Carry_Run'],
        sprint_carry='clamp carry speed blend at Pistol_Carry_Run, but use the Sprint normalized phase',
        aim='preserve existing Aim_Walk/Aim_Run 2D direction and shared phase; do not rotate a forward start/stop to replace strafing',
        reload='existing Reload_Pistol upper-body override, ammo controlled by game',
        floor='after masked blending, apply the established visual pelvis/sole floor correction; Actor root remains fixed'),
    clips={c['clip']:c for c in old if c['clip'] in specs},
    preserved=['all previous 63 HUMAN_7H actions','all other rigs and animations','23 bone bind and skin','all meshes/materials/equipment offsets','both existing project entry scenes'],
    runtime_cache_edited=False,game_code_edited=False,
    references=dict(provider='Mixamo',checked_on='2026-10-29',method='public motion previews observed; keys authored directly on existing HUMAN_7H, no files downloaded or retargeted',
        motions=[dict(title='Fast Run',description='Running Fast',url='https://www.mixamo.com/#/?page=1&query=run&type=Motion%2CMotionPack',
                      observed='forward torso lean, bent elbows, stronger knee recovery and shorter stance'),
                 dict(title='Run To Stop',description='Fast Stop From Full Run',url='https://www.mixamo.com/#/?page=1&query=stop&type=Motion%2CMotionPack',
                      observed='braking plant, lower center of mass and recovery step; shortened and kept in place'),
                 dict(title='Female Stop And Start Walking',url='https://www.mixamo.com/#/?page=1&query=stop&type=Motion%2CMotionPack',
                      observed='animated card preview for weight transfer between stationary stance and first step; no motion data imported')]))
write('Documentation/acceleration_blending.json',contract)
civic=read('Documentation/civic_animation_contract.json');civic['humanoid_clips']=len(old);civic['human_new_clips']=len(old)-15
civic['acceleration']={'contract':'acceleration_blending.json','new_clips':19,'previous_63_preserved':True}
write('Documentation/civic_animation_contract.json',civic)
print('ACCELERATION_CONTRACT',len(old),'HUMAN CLIPS')
