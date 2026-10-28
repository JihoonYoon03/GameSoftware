"""Check fitted equipment on all ten source assemblies before GLB delivery."""
import ast,bpy,json,math,sys
from pathlib import Path
from mathutils import Matrix,Quaternion,Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import human_pose
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
profiles=json.loads((ROOT/'Documentation/body_profiles.json').read_text(encoding='utf-8'))
fittings=json.loads((ROOT/'Documentation/accessory_fitting_offsets.json').read_text(encoding='utf-8'))
MATS={m.name:m for m in bpy.data.materials}
PROTOTYPES={a['id']:bpy.data.objects.get(a['id']) for a in catalog['assets'] if a.get('skeleton')=='HUMAN_7H'}
human=bpy.data.objects['HUMAN_7H'];scene=bpy.data.scenes.new('Civic_Equipment_QA');scene.render.fps=30;bpy.context.window.scene=scene
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='clone_actor'],type_ignores=[]),'actor_clone_helper','exec'),globals())
tree=ast.parse((ROOT/'Tools/verify_civic_animations.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ['bind','points']],type_ignores=[]),'actual_skin_helpers','exec'),globals())
report={'characters':{},'intersections':[]}
selection=['Walk','Run','Aim_Base','Attack_Fire','Hit_Front','Hit_Back','Hit_Left','Hit_Right',
           'Knockdown_Front','Knockdown_Back','Knockdown_Left','Knockdown_Right',
           'Downed_Loop','GetUp_Front','GetUp_Back','GetUp_Left','GetUp_Right']
selection += [c['clip'] for c in json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))['HUMAN_7H'] if c['clip'].startswith(('Aim_Walk_','Aim_Run_'))]
assemblies={n:info for n,info in recipes.items() if isinstance(info,dict) and 'body' in info}
templates={}
for info in assemblies.values():templates.setdefault(info['role'],info)
for profile in ([] if '--original-only' in sys.argv else profiles):
    for role,template in templates.items():
        assemblies['Fit_'+profile+'_'+role]={**template,'body':profile,'parts':[p.replace(template['body'],profile) for p in template['parts']],
                                           'offsets':fittings[profile][role]}
for name,info in assemblies.items():
    col=bpy.data.collections.new('EQUIPMENT_QA_'+name);scene.collection.children.link(col)
    rig,meshes=clone_actor('Equipment_QA_'+name,human,info['parts'],col,skin=info.get('skin'),part_offsets=info.get('offsets'))
    selected=[o for o in meshes if o.get('asset_id').startswith(('Head_','Hair_','Helmet_','Hat_','Weapon_')) or any(k in o.get('asset_id') for k in ['TwinTank','PowerRig','Combat_Reactor','RescuePack','SampleRack','CompactPack','Rucksack'])]
    for clip in selection:
        action=bpy.data.actions['HUMAN_7H__'+clip]
        for i in range(9):
            if '--procedural' in sys.argv:
                rig.animation_data.action=None;rig.animation_data.use_nla=False
                human_pose(rig,clip,i/8).apply();rig.update_tag();bpy.context.view_layer.update()
            else:bind(scene,rig,action,i/8)
            vertices,trees=points(selected,True)
            heads=[n for n in trees if n.startswith(('Head_','Hair_','Helmet_','Hat_'))]
            equipment=[n for n in trees if n not in heads]
            for gear in equipment:
                for part in heads:
                    if trees[gear].overlap(trees[part]):report['intersections'].append([name,clip,i/8,gear,part])
    report['characters'][name]={'body':info['body'],'clips':len(selection),'phases_per_clip':9}
    for o in meshes+[rig]:bpy.data.objects.remove(o,do_unlink=True)
    bpy.data.collections.remove(col)
    print('SOURCE_EQUIPMENT_CHECKED',name,flush=True)
report['all_checks_passed']=not report['intersections']
(ROOT/'Validation/civic_source_equipment.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('SOURCE_EQUIPMENT_RESULT',json.dumps(report['intersections']),flush=True)
assert report['all_checks_passed'],report['intersections']
