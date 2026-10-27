"""Compare the previous actions with the new procedural poses before baking."""
import bpy,ast,json,sys,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from civic_motion import human_pose
cat=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipe=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))['Player_Explorer']
PROTOTYPES={a['id']:bpy.data.objects.get(a.get('blender_object',a['id'])) for a in cat['assets']};MATS={m.name:m for m in bpy.data.materials}
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ['studio','ground','text_label','clone_actor']],type_ignores=[]),'helpers','exec'),globals())
out=ROOT/'Preview/UpperBody';out.mkdir(exist_ok=True)
for clip,phase in [('Walk',0),('Walk',.25),('Run',0),('Run',.25),('Aim_Base',0),('Aim_Walk_Left',.25),('Aim_Walk_Back',.25)]:
    s,c=studio('Pose_'+clip+str(phase),(3,-7,2.5),(0,0,1),4,(1200,820));s.cycles.samples=8;ground(c,10,-.008);bpy.context.window.scene=s
    for after,x in [(False,-.7),(True,.7)]:
        rig,obs=clone_actor(('After' if after else 'Before')+clip,bpy.data.objects['HUMAN_7H'],recipe['parts'],c,(x,0,0),recipe.get('skin'),recipe.get('offsets'))
        if clip in ['Walk','Run']:
            for ob in obs:
                if ob.get('asset_id')=='Weapon_CoilPistol_35cm':ob.hide_render=True
        if after:
            rig.animation_data.action=None;rig.animation_data.use_nla=False
            human_pose(rig,clip,phase).apply()
        else:
            old=clip if not clip.startswith('Aim_Walk') else 'Aim_Base'
            a=bpy.data.actions.get('UpperBodyV1__HUMAN_7H__'+old) or bpy.data.actions['HUMAN_7H__'+old];rig.animation_data.action=a;rig.animation_data.action_slot=a.slots[0];rig.animation_data.use_nla=False
            s.frame_set(int(a.frame_range[1]*phase),subframe=(a.frame_range[1]*phase)%1)
        text_label(c,'AFTER' if after else 'BEFORE',(x,-.55,.01),.13)
    bpy.context.view_layer.update();s.render.filepath=str(out/(clip+'_'+str(phase)+'.png'));bpy.ops.render.render(write_still=True)
    print('UPPER_BODY_DRAFT',clip,phase,flush=True)
