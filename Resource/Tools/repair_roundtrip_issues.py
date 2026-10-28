"""Ground the human turn steps and keep exported assets in one non-empty scene."""
import bpy,ast,json,math
from pathlib import Path
from mathutils import Vector,Quaternion
ROOT=Path(__file__).resolve().parents[1]
bpy.context.preferences.filepaths.save_version=0
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
names={'set_rotation','add_rotation','world_offset','reset_pose','ease','solve_chain','pose_human','clone_actor','export_glb'}
functions=ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[])
exec(compile(functions,'asset_authoring_helpers','exec'),globals())
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text())
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text())
vehicle_data=json.loads((ROOT/'Documentation/vehicle_assemblies.json').read_text())
PROTOTYPES={a['id']:bpy.data.objects[a['id']] for a in catalog['assets']}
MATS={m.name:m for m in bpy.data.materials}
scene=bpy.data.scenes['00_Library'];bpy.context.window.scene=scene
human=bpy.data.objects['HUMAN_7H']
for clip in ['Turn_Left_90','Turn_Right_90']:
    action=bpy.data.actions['HUMAN_7H__'+clip];action.fcurves.clear()
    human.animation_data.action=action;human.animation_data.action_slot=action.slots[0]
    for frame in range(25):
        reset_pose(human);pose_human(human,clip,frame/24)
        for bone in human.pose.bones:
            bone.keyframe_insert('rotation_quaternion',frame=frame);bone.keyframe_insert('location',frame=frame)
    for fc in action.fcurves:
        for key in fc.keyframe_points:key.interpolation='LINEAR'
human.animation_data.action=None;reset_pose(human)
for name,info in recipes.items():
    if 'body' not in info:continue
    temp=bpy.data.collections.new('REPAIR_'+name);scene.collection.children.link(temp)
    rig,objs=clone_actor(name+'_Final',human,info['parts'],temp,skin=info.get('skin'),part_offsets=info.get('offsets'))
    for original in human.animation_data.nla_tracks:
        track=rig.animation_data.nla_tracks.new();track.name=original.name
        strip=track.strips.new(original.name,0,original.strips[0].action);strip.name=original.name;track.mute=True
    export_glb(ROOT/'Export/Characters'/f'{name}.glb',objs,rig,True)
    for obj in objs+[rig]:bpy.data.objects.remove(obj,do_unlink=True)
    bpy.data.collections.remove(temp)
for kind,info in vehicle_data['types'].items():
    rig=bpy.data.objects[info['skeleton']]
    export_glb(ROOT/'Export/Vehicles'/f'{kind}.glb',[PROTOTYPES[n] for n in info['default_parts']],rig,True)
    export_glb(ROOT/'Export/Vehicles'/f'{kind}_Variant.glb',[PROTOTYPES[n] for n in info['alternate_parts']],rig,True)
    for choices in info['slots'].values():
        for name in choices:export_glb(ROOT/'Export/Vehicles/Parts'/f'{name}.glb',[PROTOTYPES[name]],rig,False)
bpy.context.window.scene=bpy.data.scenes['01_Characters']
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'),compress=True)
print('ROUNDTRIP_ISSUES_REPAIRED')
