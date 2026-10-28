"""Build review scenes, socket tracks and a directly openable animation project."""
import ast,bpy,json,math,sys
from pathlib import Path
from mathutils import Matrix,Vector,Quaternion
ROOT=Path(__file__).resolve().parents[1]
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
profiles=json.loads((ROOT/'Documentation/body_profiles.json').read_text(encoding='utf-8'))
fittings=json.loads((ROOT/'Documentation/accessory_fitting_offsets.json').read_text(encoding='utf-8'))
meta=json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))
vehicles=json.loads((ROOT/'Documentation/vehicle_assemblies.json').read_text(encoding='utf-8'))
MATS={m.name:m for m in bpy.data.materials}
PROTOTYPES={a['id']:bpy.data.objects.get(a.get('blender_object',a['id'])) for a in catalog['assets'] if a.get('skeleton')=='HUMAN_7H' or a.get('family','').startswith('vehicle')}
human=bpy.data.objects['HUMAN_7H']
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
names={'clone_actor','studio','ground','text_label'}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[]),'review_scene_helpers','exec'),globals())
bpy.context.preferences.filepaths.save_version=0

def bind(rig,action):
    rig.animation_data_create();rig.animation_data.action=action
    rig.animation_data.action_slot=action.slots[0];rig.animation_data.use_nla=False
    rig.update_tag()

def newscene(name,eye,target,scale,res):
    if bpy.data.scenes.get(name):
        old=bpy.data.scenes[name]
        for o in list(old.objects):
            if len(o.users_scene)==1:bpy.data.objects.remove(o,do_unlink=True)
        bpy.data.scenes.remove(old)
    scene,col=studio(name,eye,target,scale,res);scene.cycles.samples=10;ground(col,10,-.008)
    scene['motion_revision']='civic_motion_v1';scene.frame_start=0;scene.frame_end=30
    return scene,col

def actor(name,col,offset=(0,0,0),info=None):
    info=info or recipes['Player_Explorer']
    return clone_actor(name,human,info['parts'],col,offset,info.get('skin'),info.get('offsets'))

s,c=newscene('38_Motion_Before_After',(3.5,-7,2.9),(0,0,.9),4.7,(1100,760))
for name,x,prefix in [('Before',-.9,'UpperBodyV1__'),('After',.9,'')]:
    rig,obs=actor('Civic_'+name,c,(x,0,0));bind(rig,bpy.data.actions[prefix+'HUMAN_7H__Walk'])
    text_label(c,name.upper(),(x,-.48,-.02),.15)
s.frame_set(7);s['instructions']='Both actors play Walk. Before retains the original action; After is the revised gait.'

s,c=newscene('39_Civic_Body_Profile_Review',(4,-8,3.4),(0,0,.9),7.3,(1400,850))
for i,(profile,profiledata) in enumerate(profiles.items()):
    template=recipes['NPC_HeavyCombat'];body=template['body']
    parts=[p.replace(body,profile) for p in template['parts']]
    info={**template,'parts':parts,'body':profile,'offsets':fittings[profile]['HeavyCombat']}
    rig,obs=actor('Civic_Profile_'+profile,c,((i%3-1)*1.9,(i//3)*2.0,0),info)
    bind(rig,bpy.data.actions['HUMAN_7H__GetUp_Front'])
    text_label(c,profile.replace('_',' '),((i%3-1)*1.9,(i//3)*2-.50,-.02),.13)
s.frame_end=42;s.frame_set(22);s['instructions']='All six body profiles, heavy combat armor and fitted equipment; select another HUMAN_7H action for review.'

s,c=newscene('40_Bike_Rider_Contact_Review',(3.4,-4.4,2.8),(0,0,.85),3.8,(1000,800))
bike=bpy.data.objects['VEHICLE_JetBike'];info=vehicles['types']['JetBike']
rig,obs=clone_actor('Civic_Bike',bike,info['default_parts'],c)
bind(rig,bpy.data.actions['VEHICLE_JetBike__Bike_Impact'])
rider,obs=actor('Civic_Rider',c,(0,0,-.076));rider.rotation_euler.z=math.pi/2
bind(rider,bpy.data.actions['HUMAN_7H__Rider_Brace'])
for ob in obs:
    if ob.get('asset_id')=='Weapon_CoilPistol_35cm':ob.hide_render=True;ob.hide_viewport=True
s.frame_end=15;s.frame_set(3);s['instructions']='Bike_Impact and Rider_Brace share a 0.5 second clock. Weapon hidden by the riding_weapon_visibility event. Human yaw +90 degrees, translation Z=-0.076 m.'

s,c=newscene('41_Vehicle_Impact_Review',(7,-10,7),(0,0,.8),15,(1400,900))
for i,kind in enumerate(vehicles['types']):
    info=vehicles['types'][kind];rig,obs=clone_actor('Civic_Impact_'+kind,bpy.data.objects[info['skeleton']],info['default_parts'],c,((i%3-1)*5,(i//3)*3.8,0))
    bind(rig,bpy.data.actions[info['skeleton']+'__Vehicle_Impact_Front'])
    text_label(c,kind,((i%3-1)*5,(i//3)*3.8-1.4,.02),.2)
s.frame_end=15;s.frame_set(4)

s,c=newscene('42_Aim_Directional_Review',(6,-12,9),(0,1.7,1),13,(1600,1000))
for i,direction in enumerate(['Forward','ForwardLeft','Left','BackLeft','Back','BackRight','Right','ForwardRight']):
    xy=((i%4-1.5)*2.6,(i//4)*3.3,0)
    rig,obs=actor('Directional_'+direction,c,xy)
    bind(rig,bpy.data.actions['HUMAN_7H__Aim_Walk_'+direction])
    text_label(c,direction,(xy[0],xy[1]-.7,.01),.16)
s.frame_end=30;s.frame_set(7);s['instructions']='All actors face -Y. Aim_Walk/Aim_Run directional clips share phase zero = left contact. Scene 30 fps; movement and yaw belong to the game.'

if bpy.data.actions.get('HUMAN_7H__Reload_Pistol'):
    s,c=newscene('43_Pistol_Carry_Reload_Review',(3.5,-7,3.0),(0,0,1),5.2,(1200,850))
    for name,x,info in [('Player',-1.0,recipes['Player_Explorer']),('Heavy',1.0,recipes['NPC_HeavyCombat'])]:
        rig,obs=actor('Pistol_Review_'+name,c,(x,0,0),info)
        bind(rig,bpy.data.actions['HUMAN_7H__Reload_Pistol'])
        text_label(c,name+' / RELOAD',(x,-.62,.01),.16)
    s.frame_end=42;s.frame_set(20)
    s['instructions']='Reload_Pistol: 42 frames. Select Pistol_Carry_Idle/Walk/Run for 60/30/20-frame loops. Upper-body mask excludes root/pelvis/legs. Ammo remains game-owned.'

# Actual muzzle positions, sampled from the baked rig at 60 Hz. No runtime VFX
# geometry is created; the consumer can attach the existing 2D billboard here.
scene=bpy.data.scenes.new('Civic_Socket_Track_Evaluation');scene.render.fps=30
scene.collection.objects.link(human);bpy.context.window.scene=scene
tracks={}
for clip in ['Aim_Base','Attack_Fire','Security_StunFire','Hit_Front','Hit_Back','Hit_Left','Hit_Right']+[c['clip'] for c in meta['HUMAN_7H'] if c['clip'].startswith(('Aim_Walk_','Aim_Run_','Pistol_Carry_')) or c['clip']=='Reload_Pistol']:
    action=bpy.data.actions['HUMAN_7H__'+clip];bind(human,action)
    end=action.frame_range[1];rows=[]
    for i in range(round(end*2)+1):
        f=i*.5;scene.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update()
        m=human.evaluated_get(bpy.context.evaluated_depsgraph_get()).pose.bones['muzzle'].matrix
        rows.append(dict(seconds=round(f/30,8),position_m=list(m.translation),rotation_wxyz=list(m.to_quaternion())))
    tracks[clip]=rows
human.animation_data.action=None
for p in human.pose.bones:p.matrix_basis=Matrix.Identity(4)
(ROOT/'Documentation/civic_muzzle_tracks.json').write_text(json.dumps(dict(skeleton='HUMAN_7H',socket='muzzle',space='armature, Z up, forward -Y',fps=60,tracks=tracks),indent=2)+'\n',encoding='utf-8')

# Source entry scene remains the existing transit assembly.
bpy.context.window.scene=bpy.data.scenes['16_Transit_Connected_Example']
bpy.data.scenes.remove(scene)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'),compress=True)

# Prune only in memory after saving the master, then save a normal project.
owned=[s for s in bpy.data.scenes if s.name.startswith(('37_','38_','39_','40_','41_','42_','43_'))]
bpy.context.window.scene=bpy.data.scenes['37_Civic_Animation_Review']
for s in list(bpy.data.scenes):
    if s not in owned:bpy.data.scenes.remove(s)
keep={o for s in owned for o in s.objects}
for ob in list(bpy.data.objects):
    if ob not in keep:bpy.data.objects.remove(ob,do_unlink=True)
keepcollections=set()
def collect(c):
    keepcollections.add(c)
    for child in c.children:collect(child)
for s in owned:collect(s.collection)
for c in list(bpy.data.collections):
    if c not in keepcollections:bpy.data.collections.remove(c)
for action in list(bpy.data.actions):
    if not ('HUMAN_7H__' in action.name or 'VEHICLE_' in action.name):bpy.data.actions.remove(action)
bpy.data.orphans_purge(do_local_ids=True,do_linked_ids=False,do_recursive=True)
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA'
bpy.context.scene.frame_set(0)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source/Civic_Animations.blend'),compress=True)
print('CIVIC_EDITABLE_PROJECT_SAVED',len(owned),'SCENES',flush=True)
