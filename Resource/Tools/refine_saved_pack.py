"""Small final edits to saved review layout and vehicle hinges, then re-export vehicles."""
import bpy,json,math,hashlib
from pathlib import Path
from mathutils import Vector,Quaternion
ROOT=Path(__file__).resolve().parents[1]
bpy.context.preferences.filepaths.save_version=0
data=json.loads((ROOT/'Documentation/vehicle_assemblies.json').read_text())
# Retarget the low-profile lids upwards; tall freight/shuttle doors hinge outwards.
for kind,info in data['types'].items():
    rig=bpy.data.objects[info['skeleton']]
    basis=rig.data.bones['cargo'].matrix_local.to_quaternion()
    for clip in ['Cargo_Open','Cargo_Close']:
        action=bpy.data.actions[rig.name+'__'+clip]
        for fc in action.fcurves:
            if fc.data_path!='pose.bones["cargo"].rotation_quaternion':continue
            for key in fc.keyframe_points:
                t=key.co.x/30;u=t*t*(3-2*t);u=u if clip.endswith('Open') else 1-u
                angle=(.75 if kind in ['Hauler','Shuttle'] else -.75)*u
                quat=basis.inverted()@Quaternion((0,1,0),angle)@basis
                key.co.y=quat[fc.array_index]
# Match the optional cannon's muzzle to the actual barrel height on all vehicle rigs.
seen=set()
for scene in bpy.data.scenes:
    for rig in list(scene.objects):
        if rig.type!='ARMATURE' or not rig.get('vehicle_type') or rig.data.as_pointer() in seen:continue
        seen.add(rig.data.as_pointer());kind=rig['vehicle_type'];H=data['types'][kind]['parameters']['height']
        bpy.context.window.scene=scene
        for obj in bpy.context.selected_objects:obj.select_set(False)
        rig.select_set(True);bpy.context.view_layer.objects.active=rig;bpy.ops.object.mode_set(mode='EDIT')
        for name in ['turret_pitch','muzzle']:
            b=rig.data.edit_bones[name];delta=.18+H*.99-b.head.z
            b.head.z+=delta;b.tail.z+=delta
        bpy.ops.object.mode_set(mode='OBJECT');rig.select_set(False)

s=bpy.data.scenes['08_Outfit_Purposes']
for obj in s.objects:
    if obj.type=='ARMATURE' and obj.name.startswith('Purpose_') and obj.location.y>2:obj.location.y=4.5
    if obj.type=='FONT':
        if obj.data.body.startswith('NINE PURPOSES'):obj.location.y=5.4
        elif obj.data.body in ['Miner','HeavyCombat','Pilot','Civilian']:obj.location.y=4.02
s.camera.rotation_euler=(Vector((0,2.2,1))-s.camera.location).to_track_quat('-Z','Y').to_euler()
if '08b_Outfit_Silhouette_Check' not in bpy.data.scenes:
    gray=s.copy();gray.name='08b_Outfit_Silhouette_Check';gray.use_fake_user=True
else:gray=bpy.data.scenes['08b_Outfit_Silhouette_Check']
gray.view_layers[0].material_override=bpy.data.materials['QA_Neutral_Gray']
gray.use_fake_user=True

def reset(rig):
    for b in rig.pose.bones:b.rotation_mode='QUATERNION';b.rotation_quaternion=(1,0,0,0);b.location=(0,0,0);b.scale=(1,1,1)
def export(file,rig,names,animate):
    bpy.context.window.scene=bpy.data.scenes['00_Library']
    for o in bpy.context.selected_objects:o.select_set(False)
    rig.select_set(True);bpy.context.view_layer.objects.active=rig
    for name in names:bpy.data.objects[name].select_set(True)
    rig.animation_data.action=None;reset(rig)
    for track in rig.animation_data.nla_tracks:track.mute=not animate
    bpy.context.scene.frame_set(0)
    bpy.ops.export_scene.gltf(filepath=str(ROOT/file),export_format='GLB',use_selection=True,use_active_scene=True,
      export_animations=animate,export_animation_mode='NLA_TRACKS',export_nla_strips=True,
      export_force_sampling=True,export_frame_range=False,export_frame_step=1,export_skins=True,
      export_def_bones=False,export_yup=True,export_extras=True,export_cameras=False,export_lights=False,
      export_materials='EXPORT',export_apply=False,export_texcoords=True,export_normals=True,
      export_image_format='AUTO',export_keep_originals=False,export_anim_slide_to_zero=True)
    for track in rig.animation_data.nla_tracks:track.mute=True
    reset(rig)

skels=json.loads((ROOT/'Documentation/skeletons.json').read_text())
for kind,info in data['types'].items():
    rig=bpy.data.objects[info['skeleton']]
    export(f'Export/Vehicles/{kind}.glb',rig,info['default_parts'],True)
    export(f'Export/Vehicles/{kind}_Variant.glb',rig,info['alternate_parts'],True)
    for choices in info['slots'].values():
        for name in choices:export(f'Export/Vehicles/Parts/{name}.glb',rig,[name],False)
    bones=[{'name':b.name,'parent':b.parent.name if b.parent else None,'head_m':list(b.head_local),'tail_m':list(b.tail_local),'inverse_bind_matrix_column_major':[x for col in b.matrix_local.inverted().transposed() for x in col]} for b in rig.data.bones]
    skels[rig.name]['bones']=bones;skels[rig.name]['signature']=hashlib.sha256(json.dumps(bones,sort_keys=True).encode()).hexdigest()
(ROOT/'Documentation/skeletons.json').write_text(json.dumps(skels,indent=2),encoding='utf-8')
bpy.context.window.scene=bpy.data.scenes['01_Characters']
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'),compress=True)
print('SAVED_PACK_REFINED',len(bpy.data.scenes),'SCENES')
