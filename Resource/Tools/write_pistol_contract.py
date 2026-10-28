"""Document the four additional layers and the default Mixamo reference policy."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
path=ROOT/'Documentation/animation_clips.json';meta=json.loads(path.read_text(encoding='utf-8'))
new=['Pistol_Carry_Idle','Pistol_Carry_Walk','Pistol_Carry_Run','Reload_Pistol'];clips={c['clip']:c for c in meta['HUMAN_7H']}
assert set(new)<=clips.keys()
mask=json.loads((ROOT/'Documentation/animation_blending.json').read_text(encoding='utf-8'))['masks']['human_upper_body']
contract=dict(revision='pistol_layers_v1',date='2026-10-28',request='../ANIMATION_BLEND_REQUEST.md',skeleton='HUMAN_7H',bones=23,
    total_human_clips=len(clips),previous_clips_preserved=59,new_clips=new,upper_body_mask=mask,excluded_bones=['root','pelvis','thigh.L','shin.L','foot.L','thigh.R','shin.R','foot.R'],
    clips={n:clips[n] for n in new},aim_transition_seconds=[.1,.2],recommended_aim_transition_seconds=.15,
    movement_phase={'left_contact':0,'right_contact':.5,'walk_seconds':1,'run_seconds':2/3,'reset_on_mode_switch':False},
    reload={'ammo_authority':'game code only','visual_events_change_ammo':False,'separate_magazine_mesh_added':False,'gesture':'right hand holds the grip; left hand visits magazine well and slide; existing rigid pistol reused','entry_exit':'Pistol_Carry_Idle at phase 0'},
    future_speed_extension='No sprint or faster aim sample added; request marks this as future work after target actor speeds are settled.',runtime_cache_edited=False,
    references={'default_provider':'Mixamo','policy':'view the relevant public motion before authoring; record actual title, date and observed features; do not claim a downloaded retarget when hand-authoring',
        'checked_on':'2026-10-28','method':'public 3D preview observed; hand-authored on the existing rig, no motion data imported',
        'motions':[{'title':'Pistol Run','description':'Running With A Pistol','url':'https://www.mixamo.com/#/?page=1&query=pistol&type=Motion%2CMotionPack','use':'weapon arm damping and bent-elbow carry timing; adapted to one-handed lowered carry'},
                   {'title':'Pistol Aim','description':'Pistol Aim To Holster Idle','url':'https://www.mixamo.com/#/?page=1&query=pistol&type=Motion%2CMotionPack','use':'lowering and recovery trajectory between carry and aiming'},
                   {'title':'Reloading','description':'Gun Reload While Walking','url':'https://www.mixamo.com/#/?page=1&query=reload&type=Motion%2CMotionPack','use':'stable weapon hand, separate magazine and slide hand beats; compressed to the requested 1.4 seconds'}]})
(ROOT/'Documentation/pistol_layer_contract.json').write_text(json.dumps(contract,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
path=ROOT/'Documentation/animation_blending.json';blend=json.loads(path.read_text(encoding='utf-8'))
blend['pistol_layers']={'contract':'pistol_layer_contract.json','carry_samples':new[:3],'reload':new[3],'mask':'human_upper_body','keep_locomotion_phase':True,'aim_crossfade_seconds':.15,'ammo_changes_from_events':False}
path.write_text(json.dumps(blend,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
path=ROOT/'Documentation/civic_animation_contract.json';civic=json.loads(path.read_text(encoding='utf-8'))
civic['humanoid_clips']=len(clips);civic['human_new_clips']=len(clips)-15;civic['pistol_layers']={'contract':'pistol_layer_contract.json','new_clips':4,'existing_59_preserved':True}
path.write_text(json.dumps(civic,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print('PISTOL_CONTRACT_WRITTEN',len(clips),flush=True)
