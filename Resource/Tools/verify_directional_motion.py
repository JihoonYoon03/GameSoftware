"""Baked poses, stance contacts and adjacent-direction blend checks."""
import bpy,json,math,sys
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import Pose,blend,DIRECTION_ANGLES,aim_speed
rig=bpy.data.objects['HUMAN_7H'];scene=bpy.data.scenes.new('Directional_QA');scene.collection.objects.link(rig);scene.render.fps=30;bpy.context.window.scene=scene
meta=json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'));clips=[c for c in meta['HUMAN_7H'] if c['clip'].startswith(('Aim_Walk_','Aim_Run_'))]
report={'clips':{},'upper_body':{},'blend_samples':0,'max_blended_sole_penetration_m':0,'max_blend_contact_correction_m':0,'max_corrected_sole_penetration_m':0,'max_blended_grip_error_m':0,'all_checks_passed':False}
boot_objects=[bpy.data.objects['Boot_'+s] for s in ['L','R']]
def boot_overlap(p):
    trees=[]
    for ob in boot_objects:
        points=[sum((p.point(ob.vertex_groups[g.group].name,v.co)*g.weight for g in v.groups),Vector()) for v in ob.data.vertices]
        trees.append(BVHTree.FromPolygons(points,[f.vertices[:] for f in ob.data.polygons]))
    return bool(trees[0].overlap(trees[1]))
def sample(name,t):
    a=bpy.data.actions['HUMAN_7H__'+name];rig.animation_data.action=a;rig.animation_data.action_slot=a.slots[0];rig.animation_data.use_nla=False
    rig.update_tag();scene.frame_set(-1);f=a.frame_range[1]*t;scene.frame_set(int(f),subframe=f%1);bpy.context.view_layer.update()
    evaluated=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    p=Pose(rig);p.basis={b.name:b.matrix_basis.copy() for b in evaluated.pose.bones};return p
def grip(p):
    delta=p.deformation('hand.R').to_quaternion();expected=p.fk('hand.R').translation+delta@Vector((.030,-.020,.065))
    return (p.fk('hand.L').translation-expected).length
def sole(p):
    return min(p.point('foot.'+s,(sign*.115,y,0)).z for s,sign in [('L',1),('R',-1)] for y in [-.195,.065])
def muzzle_error(p):
    m=p.fk('muzzle');v=m.to_3x3()@Vector((0,1,0));return math.degrees(v.normalized().angle(Vector((0,-1,0))))
for c in clips:
    name=c['clip'];poses=[sample(name,i/32) for i in range(33)];first=poses[0]
    seam=max(abs(first.fk(n)[i][j]-poses[-1].fk(n)[i][j]) for n in first.rest for i in range(4) for j in range(4))
    root=max(max(abs(p.fk('root')[i][j]-first.fk('root')[i][j]) for i in range(4) for j in range(4)) for p in poses)
    ge=max(grip(p) for p in poses);me=max(muzzle_error(p) for p in poses);low=min(sole(p) for p in poses)
    separation=min(p.fk('foot.L').translation.x-p.fk('foot.R').translation.x for p in poses)
    overlaps=[i/32 for i,p in enumerate(poses) if boot_overlap(p)]
    assert seam<2e-5 and root<1e-6 and ge<.0005 and me<.1,(name,seam,root,ge,me)
    assert low>=-.005 and separation>.015,(name,'soles/crossed feet',low,separation)
    assert not overlaps,(name,'actual boot surfaces intersect',overlaps)
    # The flat middle of left stance follows the opposite of actor velocity.
    fast=name.startswith('Aim_Run_');a,b=(.12,.22) if fast else (.14,.40);pa,pb=sample(name,a),sample(name,b)
    va,vb=pa.fk('foot.L').translation.copy(),pb.fk('foot.L').translation.copy();velocity=Vector(c['travel_direction_local'])*c['speed_mps']
    drift=(vb+velocity*c['duration_seconds']*(b-a)-va).length
    assert drift<.003,(name,'stance slides',drift)
    report['clips'][name]=dict(loop_matrix_error=seam,root_error=root,grip_error_m=ge,muzzle_angle_degrees=me,min_sole_z_m=low,min_foot_separation_m=separation,stance_drift_m=drift)
for clip in ['Walk','Run']:
    current=[];previous=[]
    lower_error=0
    for i in range(33):
        p=sample(clip,i/32);a=bpy.data.actions['UpperBodyV1__HUMAN_7H__'+clip];rig.animation_data.action=a;rig.animation_data.action_slot=a.slots[0]
        rig.update_tag();f=a.frame_range[1]*i/32;scene.frame_set(-1);scene.frame_set(int(f),subframe=f%1);bpy.context.view_layer.update()
        evaluated=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
        old=Pose(rig);old.basis={b.name:b.matrix_basis.copy() for b in evaluated.pose.bones}
        for pose,out in [(p,current),(old,previous)]:out.append(max(abs((pose.fk('forearm.'+s).translation-pose.fk('upper_arm.'+s).translation).x) for s in ['L','R']))
        for n in ['root','pelvis','thigh.L','thigh.R','shin.L','shin.R','foot.L','foot.R']:
            lower_error=max(lower_error,max(abs(p.fk(n)[r][k]-old.fk(n)[r][k]) for r in range(4) for k in range(4)))
    assert max(current)<max(previous)*.75,(clip,'elbows still flare',max(current),max(previous))
    assert lower_error<.003,(clip,'existing lower-body motion changed',lower_error)
    report['upper_body'][clip]=dict(previous_elbow_lateral_excursion_m=max(previous),current_elbow_lateral_excursion_m=max(current),lower_body_matrix_error=lower_error)
directions=list(DIRECTION_ANGLES)
for speed in ['Walk','Run']:
    for a,b in zip(directions,directions[1:]+directions[:1]):
        for t in [i/8 for i in range(8)]:
            pa,pb=sample('Aim_'+speed+'_'+a,t),sample('Aim_'+speed+'_'+b,t)
            for w in [.25,.5,.75]:
                p=blend(pa,pb,w);report['blend_samples']+=1
                report['max_blended_sole_penetration_m']=max(report['max_blended_sole_penetration_m'],-sole(p))
                report['max_blended_grip_error_m']=max(report['max_blended_grip_error_m'],grip(p))
                assert max(abs(p.fk('root')[i][j]-pa.fk('root')[i][j]) for i in range(4) for j in range(4))<1e-6
                correction=max(0,.001-sole(p));p.translate('pelvis',(0,0,correction))
                report['max_blend_contact_correction_m']=max(report['max_blend_contact_correction_m'],correction)
                report['max_corrected_sole_penetration_m']=max(report['max_corrected_sole_penetration_m'],-sole(p))
assert report['max_corrected_sole_penetration_m']<.00005 and report['max_blended_grip_error_m']<.0005,report
report['all_checks_passed']=True
(ROOT/'Validation/directional_motion_report.json').write_text(json.dumps(report,indent=2)+'\n')
print('DIRECTIONAL_MOTION_VERIFIED',json.dumps(report),flush=True)
