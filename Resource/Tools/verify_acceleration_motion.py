"""Check stored key interpolation, phase handoff, static root and actual skin clearance."""
import ast,bpy,json,sys,math
from pathlib import Path
import numpy as np
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import Pose
from acceleration_motion import SPECS
human=bpy.data.objects['HUMAN_7H'];rig=human
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
PROTOTYPES={a['id']:bpy.data.objects.get(a.get('blender_object',a['id'])) for a in catalog['assets']}
tree=ast.parse((ROOT/'Tools/build_civic_animations.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='Clearance'],type_ignores=[]),'<existing clearance>','exec'))
clearance=Clearance()
scene=bpy.data.scenes.new('Acceleration_Motion_QA');scene.render.fps=30;scene.collection.objects.link(rig);bpy.context.window.scene=scene
def sample(name,t):
    a=bpy.data.actions['HUMAN_7H__'+name];rig.animation_data.action=a;rig.animation_data.action_slot=a.slots[0]
    rig.animation_data.use_nla=False;rig.update_tag();scene.frame_set(-1)
    f=a.frame_range[0]+(a.frame_range[1]-a.frame_range[0])*t;scene.frame_set(math.floor(f),subframe=f%1);bpy.context.view_layer.update()
    e=rig.evaluated_get(bpy.context.evaluated_depsgraph_get());p=Pose(rig);p.basis={b.name:b.matrix_basis.copy() for b in e.pose.bones};return p
def error(a,b):return max(abs(a[r][c]-b[r][c]) for r in range(4) for c in range(4))
report=dict(clips={},all_checks_passed=False)
contacts={}
for name,info in SPECS.items():
    first=sample(name,0);low=999;root=0;movement=0;angle=0;prev=None;rows=[]
    # Quarter-frame checks include samples between the authored half-frame keys.
    for i in range(info['frames']*4+1):
        t=i/(info['frames']*4);p=sample(name,t);low=min(low,clearance.minimum(p))
        root=max(root,error(p.basis['root'],Matrix.Identity(4)))
        movement=max(movement,max((p.fk(n).translation-first.fk(n).translation).length for n in p.rest))
        if prev:
            angle=max(angle,max(p.basis[n].to_quaternion().rotation_difference(prev.basis[n].to_quaternion()).angle for n in p.rest))
        soles={s:min(p.point('foot.'+s,v.co).z for v in PROTOTYPES['Boot_'+s].data.vertices) for s in ['L','R']}
        if i%2==0:rows.append(dict(seconds=i/120,left_sole_z_m=soles['L'],right_sole_z_m=soles['R'],left_contact=soles['L']<.004,right_contact=soles['R']<.004))
        prev=p
    assert low>=-.005,(name,'interpolated skin below floor',low)
    assert root<2e-5 and movement>.01,(name,'static or moving root',root,movement)
    assert angle<.75,(name,'abrupt joint rotation',angle)
    endpoints={}
    if info['loop']:
        seam=max(error(first.fk(n),prev.fk(n)) for n in first.rest);assert seam<2e-4
    else:
        seam=None
        for label,t,base,phase in [('entry',0,info['source'],info['entry_phase']),('exit',1,info['target'],info['exit_phase'])]:
            p=sample(name,t);q=sample(base,phase)
            err=max(error(p.fk(n),q.fk(n)) for n in p.rest);endpoints[label]=err
            assert err<.0005,(name,label,'handoff mismatch',err)
    report['clips'][name]=dict(min_fitted_skin_z_m=low,max_root_error=root,max_120hz_joint_step_radians=angle,
        loop_error=seam,endpoint_errors=endpoints,max_joint_translation_movement_m=movement)
    contacts[name]=rows
    print('ACCELERATION_MOTION_CHECKED',name,round(low,5),round(angle,4),flush=True)
report['all_checks_passed']=True
(ROOT/'Validation/acceleration_motion_report.json').write_text(json.dumps(report,indent=2)+'\n')
(ROOT/'Documentation/acceleration_foot_contacts.json').write_text(json.dumps(dict(sample_rate_hz=60,contact_threshold_m=.004,space='flat authoring floor; visual support hints, not gameplay collision',clips=contacts),indent=2)+'\n')
print('ACCELERATION_MOTION_PASSED',len(report['clips']),flush=True)
