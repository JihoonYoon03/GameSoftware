"""Measure contact anchors and upper-body layering on the delivered source."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[1]
scene=bpy.data.scenes['00_Library'];bpy.context.window.scene=scene
rig=bpy.data.objects['HUMAN_7H'];report={}
def sample(clip,phase):
    global evaluated
    a=bpy.data.actions['HUMAN_7H__'+clip]
    changed=rig.animation_data.action!=a
    rig.animation_data.action=a;rig.animation_data.action_slot=a.slots[0];rig.animation_data.use_nla=False
    f=a.frame_range[1]*phase
    if changed:rig.update_tag();scene.frame_set(-1)
    scene.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update()
    evaluated=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
def matrix(n):return evaluated.pose.bones[n].matrix.copy()
def point(n,p):return matrix(n)@rig.data.bones[n].matrix_local.inverted()@Vector(p)
def error(a,b):return max(abs(a[i][j]-b[i][j]) for i in range(4) for j in range(4))
for clip,speed,period,ranges in [('Walk',1.08,1.,[('heel',.13,.40),('toe',.49,.59)]),('Run',2.46,2/3,[('heel',.12,.22),('toe',.28,.39)])]:
    checks={}
    for label,start,end in ranges:
        coords=[]
        for i in range(21):
            t=start+(end-start)*i/20;sample(clip,t)
            p=point('foot.L',(.115,.065 if label=='heel' else -.195,0))
            p.y-=speed*period*t;coords.append(p)
        drift=max((Vector((p.x,p.y,0))-Vector((coords[0].x,coords[0].y,0))).length for p in coords)
        print('CONTACT_RANGE',clip,label,'START',tuple(coords[0]),'END',tuple(coords[-1]),'DRIFT',drift,flush=True)
        # X retains the authored foot lane; Y compensates the documented Actor speed.
        assert drift<.003,(clip,label,'stance drift',drift)
        checks[label]={'compensated_horizontal_drift_m':drift,'phase_range':[start,end]}
    report[clip]=checks

sample('Aim_Base',0);grip=matrix('hand.R').inverted()@matrix('hand.L')
max_grip=0
for clip in ['Aim_Base','Attack_Fire','Security_StunFire','Hit_Front','Hit_Back','Hit_Left','Hit_Right']:
    for i in range(25):
        sample(clip,i/24);current=matrix('hand.R').inverted()@matrix('hand.L')
        max_grip=max(max_grip,error(grip,current))
assert max_grip<.0002,('support grip drift',max_grip)
report['weapon_grip_max_matrix_error']=max_grip

lower=['root','pelvis','thigh.L','shin.L','foot.L','thigh.R','shin.R','foot.R']
sample('Aim_Base',0);reference={n:evaluated.pose.bones[n].matrix_basis.copy() for n in lower}
maximum=0
for clip in ['Hit_Front','Hit_Back','Hit_Left','Hit_Right','Security_StunFire']:
    for i in range(13):
        sample(clip,i/12)
        maximum=max(maximum,max(error(reference[n],evaluated.pose.bones[n].matrix_basis) for n in lower))
assert maximum<.0001,('reaction lower-body delta',maximum)
report['masked_hit_lower_body_max_matrix_error']=maximum

contract=json.loads((ROOT/'Documentation/civic_animation_contract.json').read_text(encoding='utf-8'))
ride=contract['rider'];transform=Matrix.Translation(Vector(ride['authoring_to_bike_translation_m']))@Matrix.Rotation(math.pi/2,4,'Z')
expected={'seat':Vector((.112884,0,.818092))}
expected.update({'hand.'+s:Vector(v) for s,v in ride['handlebar_contact_vehicle_m'].items()})
expected.update({'foot.'+s:Vector(v) for s,v in ride['footrest_contact_vehicle_m'].items()})
initial=None;drift=0;alignment=0
for i in range(31):
    sample('Rider_Brace',i/30)
    anchors={}
    for s in ['L','R']:
        anchors['hand.'+s]=transform@point('hand.'+s,ride['hand_contact_reference_bind_points'][s])
        anchors['foot.'+s]=transform@point('foot.'+s,ride['foot_sole_reference_bind_points'][s])
    anchors['seat']=transform@point('pelvis',(0,-.114,.91))
    if initial is None:initial=anchors
    drift=max(drift,max((v-initial[n]).length for n,v in anchors.items()))
    alignment=max(alignment,max((v-expected[n]).length for n,v in anchors.items()))
assert drift<.0001,('rider contact drift',drift)
assert alignment<.002,('rider attachment alignment',alignment)
report['rider']={'max_contact_drift_m':drift,'max_anchor_alignment_error_m':alignment,'anchors_vehicle_m':{n:list(v) for n,v in initial.items()}}

sample('GetUp_Front',.26)
hand=bpy.data.objects['Body_Hand_L']
hand_low=min(point('hand.L',v.co).z for v in hand.data.vertices)
report['getup_left_hand_min_z_at_support_m']=hand_low
assert -.005<hand_low<.055,('get-up supporting hand misses floor',hand_low)
report['all_checks_passed']=True
(ROOT/'Validation/civic_contact_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('CIVIC_CONTACT_CHECKS_PASSED',json.dumps(report),flush=True)
