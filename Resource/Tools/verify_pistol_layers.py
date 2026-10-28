"""Check the added layers and carry/aim blends on real fitted character meshes."""
import bpy,ast,json,sys
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import Pose
from pistol_layer_motion import SPECS,LOWER
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
profiles=json.loads((ROOT/'Documentation/body_profiles.json').read_text(encoding='utf-8'))
fittings=json.loads((ROOT/'Documentation/accessory_fitting_offsets.json').read_text(encoding='utf-8'))
MATS={m.name:m for m in bpy.data.materials};PROTOTYPES={a['id']:bpy.data.objects.get(a.get('blender_object',a['id'])) for a in catalog['assets']}
human=bpy.data.objects['HUMAN_7H'];scene=bpy.data.scenes.new('Pistol_Layer_QA');scene.render.fps=30;scene.collection.objects.link(human);bpy.context.window.scene=scene
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='clone_actor'],type_ignores=[]),'actor_helpers','exec'),globals())
tree=ast.parse((ROOT/'Tools/verify_civic_animations.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='points'],type_ignores=[]),'skin_helpers','exec'),globals())
cache={}
def sample(name,t):
    key=(name,t)
    if key in cache:return cache[key]
    a=bpy.data.actions['HUMAN_7H__'+name];human.animation_data.action=a;human.animation_data.action_slot=a.slots[0];human.animation_data.use_nla=False;human.update_tag()
    scene.frame_set(-1);f=a.frame_range[1]*t;scene.frame_set(int(f),subframe=f%1);bpy.context.view_layer.update()
    e=human.evaluated_get(bpy.context.evaluated_depsgraph_get());p=Pose(human);p.basis={b.name:b.matrix_basis.copy() for b in e.pose.bones};cache[key]=p;return p
report={'characters':{},'intersections':[],'clips':{},'blend_samples':0,'all_checks_passed':False}
for name,(base,end,loop) in SPECS.items():
    error=0;down=1
    for i in range(17):
        p=sample(name,i/16);b=sample(base,0 if name=='Reload_Pistol' else i/16)
        error=max(error,max(abs(p.basis[n][r][c]-b.basis[n][r][c]) for n in LOWER for r in range(4) for c in range(4)))
        down=min(down,-(p.fk('muzzle').to_3x3()@Vector((0,1,0))).z)
    assert error<2e-5,(name,'lower body changed',error)
    if name!='Reload_Pistol':assert down>.65,(name,'unsafe muzzle direction',down)
    report['clips'][name]={'lower_body_matrix_error':error,'minimum_downward_muzzle_component':down}
carry_start=sample('Pistol_Carry_Idle',0)
for phase in [0,1]:
    reload=sample('Reload_Pistol',phase)
    error=max(abs(reload.fk(n)[r][c]-carry_start.fk(n)[r][c]) for n in reload.rest for r in range(4) for c in range(4))
    assert error<2e-5,('reload/carry boundary',phase,error)
    report['clips']['Reload_Pistol']['boundary_'+str(phase)+'_matrix_error']=error
assemblies={n:info for n,info in recipes.items() if isinstance(info,dict) and 'body' in info};templates={}
for info in assemblies.values():templates.setdefault(info['role'],info)
for profile in profiles:
    for role,template in templates.items():assemblies['Fit_'+profile+'_'+role]={**template,'body':profile,'parts':[p.replace(template['body'],profile) for p in template['parts']], 'offsets':fittings[profile][role]}
upper=[b.name for b in human.data.bones if b.name not in LOWER]
aim=sample('Aim_Base',0)
def mixed(a,b,w):
    p=Pose(human);p.basis={n:m.copy() for n,m in a.basis.items()}
    for n in upper:
        la,qa,sa=a.basis[n].decompose();lb,qb,sb=b.basis[n].decompose();p.basis[n]=Matrix.LocRotScale(la.lerp(lb,w),qa.slerp(qb,w),sa.lerp(sb,w))
    return p
for number,(name,info) in enumerate(assemblies.items()):
    col=bpy.data.collections.new('Pistol_QA_'+name);scene.collection.children.link(col)
    ids=list(info['parts'])
    if 'Weapon_CoilPistol_35cm' not in ids:ids.append('Weapon_CoilPistol_35cm')
    rig,meshes=clone_actor('Pistol_QA_'+name,human,ids,col,skin=info.get('skin'),part_offsets=info.get('offsets'));rig.animation_data.action=None;rig.animation_data.use_nla=False
    tests=[]
    for clip in SPECS:
        steps=16 if clip=='Reload_Pistol' else 8
        for i in range(steps+1):tests.append((clip,i/steps,sample(clip,i/steps)))
    for clip in ['Pistol_Carry_Idle','Pistol_Carry_Walk','Pistol_Carry_Run']:
        for t in [0,.25,.5,.75]:
            for w in [.25,.5,.75]:tests.append((clip+'->Aim_Base',t,mixed(sample(clip,t),aim,w)));report['blend_samples']+=1
    for clip,t,p in tests:
        for b in rig.pose.bones:b.matrix_basis=p.basis[b.name]
        rig.update_tag();bpy.context.view_layer.update();vertices,trees=points(meshes,True);weapon=trees['Weapon_CoilPistol_35cm']
        for part,tree in trees.items():
            if part=='Weapon_CoilPistol_35cm' or part.startswith('Body_Hand_') or ('Cuff_' in part):continue
            if weapon.overlap(tree):report['intersections'].append([name,clip,round(t,4),part])
    report['characters'][name]={'body':info['body'],'pose_samples':len(tests)}
    for o in meshes+[rig]:bpy.data.objects.remove(o,do_unlink=True)
    bpy.data.collections.remove(col)
    if number%8==0:print('PISTOL_EQUIPMENT_CHECKED',number,len(report['intersections']),flush=True)
report['all_checks_passed']=not report['intersections']
(ROOT/'Validation/pistol_layer_validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PISTOL_LAYER_QA',len(report['characters']),'CHARACTERS',len(report['intersections']),'INTERSECTIONS',report['intersections'][:20],flush=True)
assert report['all_checks_passed'],report['intersections'][:20]
