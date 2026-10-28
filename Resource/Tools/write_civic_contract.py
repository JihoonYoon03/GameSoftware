"""Write the asset-side integration contract; never edits the game or Runtime."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(name):return json.loads((ROOT/'Documentation'/name).read_text(encoding='utf-8'))
def write(name,data):(ROOT/'Documentation'/name).write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
clips=read('animation_clips.json');blend=read('animation_blending.json');vehicles=read('vehicle_animation_blending.json')
lower=blend['masks']['human_lower_body'];upper=blend['masks']['human_upper_body']
directions={d:{'knockdown':'Knockdown_'+d,'downed':'Downed_Loop' if d=='Front' else 'Downed_'+d+'_Loop','getup':'GetUp_'+d,'hit':'Hit_'+d} for d in ['Front','Back','Left','Right']}
human_contacts={'Walk':{'left':[[0,.60]],'right':[[0,.10],[.50,1]],'flight':[],'heel_loading':[0,.11],'heel_rise':[.44,.60]},
                'Run':{'left':[[0,.40]],'right':[[.50,.90]],'flight':[[.40,.50],[.90,1]]},
                'Flee_Run':{'left':[[0,.40]],'right':[[.50,.90]],'flight':[[.40,.50],[.90,1]]}}
for c in clips['HUMAN_7H']:
    n=c['clip']
    if n in human_contacts:
        c['contacts']=human_contacts[n];c['entry_pose']=n+':0';c['exit_pose']=n+':0'
        c['events']=[{'phase':0,'seconds':0,'event':'left_heel_contact'},{'phase':.5,'seconds':round(c['duration_seconds']*.5,6),'event':'right_heel_contact'}]
    if n=='Idle':c.update(entry_pose='Idle:0',exit_pose='Idle:0',support='both feet')
    if n=='Jump_Start':c['events']=[{'phase':.55,'seconds':.22,'event':'push_off_visual'},{'phase':1,'seconds':.4,'event':'jump_air_ready'}]
    if n=='Jump_Land':c['events']=[{'phase':.32,'seconds':.128,'event':'landing_compression'},{'phase':1,'seconds':.4,'event':'landing_recovered'}]
    if n=='Flee_Start':c['support']='left lead contact at exit; short head check then gaze on actor-supplied escape heading'
    if n=='Flee_Stop':c['support']='enter at left contact; decelerate actor over 0.2 seconds; both feet at exit'
    if n.startswith('GetUp_') or n=='Medical_GetUp':
        c['contacts']={'side_support':[0,.18],'left_hand':[.20,.31],'right_knee':[.26,.64],'left_foot':[.49,1],'both_feet':[.86,1]}
    if n.startswith('Hit_') or n=='Security_StunFire':c['support']='preserve base animation lower body; both hands retain the common grip transform'
    if n=='Ride_Base':c['support']='fixed pelvis, both palms at bars, both boot soles on pegs'
    if n=='Rider_Brace':c['support']='same five contacts as Ride_Base throughout the 0.5 second reaction'
write('animation_clips.json',clips)
blend['locomotion']['human_contact_profiles']=human_contacts
blend['locomotion']['legacy_nonhuman_stance_phase_range_per_foot']=[0,.5]
blend['locomotion']['stance_phase_range_per_foot']='HUMAN_7H: Walk 0-.60, Run 0-.40; other skeletons 0-.50'
blend['locomotion']['swing_phase_range_per_foot']='HUMAN_7H: Walk .60-1, Run .40-1; other skeletons .50-1'
blend['civic']={'contract':'civic_animation_contract.json','clips_are_automatically_registered_in_game':False,
                'runtime_cache_regenerated':False,'hit_mode':'masked_override relative to the Aim_Base reference',
                'directional_recovery':directions,'death':'hold Death_Front/Back final frame or explicit *_Pose; never loop or auto get up'}
write('animation_blending.json',blend)
vehicles['civic']={'impact':['Vehicle_Impact_Front','Vehicle_Impact_Rear','Vehicle_Impact_Side'],'disabled':'Vehicle_Disabled',
                   'bike':'Bike_Impact','rider':'Rider_Brace','chassis_and_socket_motion':'fixed, physics owns displacement and yaw',
                   'power_cut_and_sparks':'metadata events; use existing depth-tested 2D billboard',
                   'tow':'tow_cleanup_eligible_when_offscreen event; game decides delay and opacity fade; collision stays solid until removal'}
write('vehicle_animation_blending.json',vehicles)
contract={
 'revision':'civic_motion_v2_directional','request':'../CIVIC_ANIMATION_REQUEST.md','units':'meters','authoring_fps':30,'delivery_sampling_fps':60,
 'humanoid_skeleton':'HUMAN_7H','bones':23,'original_clips_preserved':15,'humanoid_clips':len(clips['HUMAN_7H']),
 'human_new_clips':len(clips['HUMAN_7H'])-15,'vehicle_new_actions':21,'human_forward':'-Y','human_left':'+X','human_up':'+Z','vehicle_forward':'+X','vehicle_left':'+Y',
 'direction_selection':{'input':'world impulse velocity transformed into character local coordinates; velocity is the direction the body is pushed',
                        'dominant_y':'vy >= 0 -> Front, vy < 0 -> Back','dominant_x':'vx <= 0 -> Left, vx > 0 -> Right','zero_vector':'retain last valid impact direction or Front',
                        'world_units':'1 world unit = 2 meters; rotate vectors before selecting; magnitude does not change visual root travel'},
 'physics_ownership':{'actor_translation':'BodyCollision only','actor_yaw':'game only for new civic reactions','root_translation':'zero in every new reaction',
                     'collision_hull':'keep existing dimensions, do not derive it from posed vertices','pelvis_motion':'local anatomical collapse/compression only',
                     'damage_application':'animation events never reapply crime, health loss or collision impulse'},
 'directional_transitions':directions,
 'layering':{'Hit_*':{'mode':'masked_override','reference':'Aim_Base','mask':upper,'excluded':lower,'derive_additive_delta':'clip local transforms relative to Aim_Base before additive use'},
             'Security_StunFire':{'mode':'masked_override','reference':'Aim_Base','mask':upper,'event_phase':.08,'event_seconds':.032,'socket':'muzzle'},
             'reaction_priority':['Dead hold','Knockdown/Downed/GetUp','Hit over Aim + locomotion','Flee/normal locomotion']},
 'recovery':{'blend_in_seconds':.08,'input_locked_until':'fully_upright at clip duration 1.4 seconds','blend_out_seconds':.12,
             'request_game_response_duration':'hold Downed_Loop until recovery is permitted, then keep input locked through the actual get-up event; do not speed it up to a shorter timer',
             'narrative_npcs':'Ria/Mara/Kain use Downed + Medical_Stabilized + Medical_GetUp; no Death routing'},
 'death':{'clips':['Death_Front','Death_Back'],'hold_clips':['Death_Front_Pose','Death_Back_Pose'],'loop':False,'automatic_getup':False,'arms_and_weapon_socket_continuous':True},
 'flee':{'start_seconds':.2,'stop_seconds':.2,'loop':'Flee_Run','escape_heading':'game supplies direction; keep gaze forward on that heading by Run entry','entry_stop_phase':0,'root_yaw':0},
 'rider':{'base':'Ride_Base','reaction':'Rider_Brace','vehicle_reaction':'Bike_Impact','duration_seconds':.5,
          'parent_space':'apply the animated chassis attachment matrix to the complete rider actor when mixing Hover_Idle/Cruise; do not bob rider root independently',
          'authoring_to_bike_rotation_z_degrees':90,'authoring_to_bike_translation_m':[0,0,-.076],
          'hand_contact_reference_bind_points':{'L':[.448,-.030,.865],'R':[-.448,-.030,.865]},
          'handlebar_contact_vehicle_m':{'L':[.300,.209,.894],'R':[.300,-.209,.894]},
          'foot_sole_reference_bind_points':{'L':[.115,0,0],'R':[-.115,0,0]},
          'footrest_contact_vehicle_m':{'L':[-.100,.340,.4425],'R':[-.100,-.340,.4425]},
          'seat_surface_vehicle_z_m':.818,'weapon_visibility_event':'hide held pistol while riding; no visibility animation in GLB',
          'existing_game_Ride':'not removed or rewritten; it is synthesized by the game packer. Ride_Base is a separately named optional authored reference. When integrating, explicitly select Ride_Base for Rider_Brace; avoid generating a second Ride name.'},
 'vfx':{'geometry_added':False,'power_cut':'emission_multiplier .12 at Vehicle_Disabled start','spark':'existing 2D billboard on engine/frontpod sockets',
        'tow_cleanup':'fade only when game allows cleanup; no root motion, airborne explosion or deletion of the collision body by animation'},
 'sources':[{'url':'https://www.ouhsc.edu/bserdac/dthompso/web/gait/terms.htm','application':'walking support timing and weight transfer'},
            {'url':'https://www.ouhsc.edu/bserdac/dthompso/web/gait/knmatics/stance.htm','application':'loading response and heel/toe roll'},
            {'url':'https://www.guysandstthomas.nhs.uk/health-information/falls/what-do-if-you-fall','application':'side roll, arm/knee support, staged rise; game choreography, not medical instruction'}],
 'authoring_method':'procedural hand-authored poses and analytic IK informed by movement references; no motion-capture recording imported',
 'runtime_implementation_included':False,
 'game_integration_checks_pending':['register exported clips and events','direction selection from impulse vector','input lock until fully_upright','no duplicate damage/crime application','narrative NPC hospital fade route','death end hold','10 surviving security / 100 residents update cost'],
 'files':{'source':'../Source/Civic_Animations.blend','master':'../Source/Erma_Resource_Library.blend','clips':'animation_clips.json','muzzle_tracks':'civic_muzzle_tracks.json','roundtrip_report':'../Validation/civic_animation_roundtrip.json'}
}
write('civic_animation_contract.json',contract)
print('CIVIC_CONTRACT_WRITTEN',contract['humanoid_clips'])
