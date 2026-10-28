"""Re-import representative exports and measure real evaluated skin deformation."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
FILES=['Export/Characters/Ria_Engineer.glb','Export/Creatures/RiftGrazer.glb',
       'Export/Creatures/VeilWalker.glb','Export/Creatures/RelaySentinel.glb',
       'Export/Modules/PressureDoor_Animated.glb',
       'Export/Characters/NPC_HeavyCombat.glb','Export/Characters/NPC_Civilian.glb',
       'Export/Vehicles/Coupe.glb','Export/Vehicles/Shuttle.glb',
       'Export/Vehicles/Hauler.glb','Export/Vehicles/Patrol_Variant.glb','Export/Vehicles/JetBike.glb']
REPORT={'files':{},'errors':[]}

def vertices(objects):
    dg=bpy.context.evaluated_depsgraph_get();result=[]
    for obj in objects:
        evaluated=obj.evaluated_get(dg);mesh=evaluated.to_mesh()
        result += [evaluated.matrix_world@v.co for v in mesh.vertices]
        evaluated.to_mesh_clear()
    return result

def sample(scene,rig,meshes,action,frame):
    rig.animation_data.action=action;scene.frame_set(int(frame),subframe=frame-int(frame))
    bpy.context.view_layer.update()
    evaluated=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    return vertices(meshes),{p.name:list(p.matrix.to_quaternion())+list(p.matrix.translation) for p in evaluated.pose.bones}

for file in FILES:
    try:
        scene=bpy.data.scenes.new('ROUNDTRIP');scene.render.fps=30;bpy.context.window.scene=scene
        before=set(bpy.data.actions);bpy.ops.import_scene.gltf(filepath=str(ROOT/file))
        actions=list(set(bpy.data.actions)-before)
        rigs=[o for o in scene.objects if o.type=='ARMATURE'];assert len(rigs)==1
        rig=rigs[0];meshes=[o for o in scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)];assert meshes
        for tr in rig.animation_data.nla_tracks:tr.mute=True
        result={'bones':len(rig.data.bones),'meshes':len(meshes),'clips':{},'transition_max_error':{}}
        for action in actions:
            clip=action.name.split('.')[0]
            first,last=action.frame_range
            initial,pose0=sample(scene,rig,meshes,action,first)
            max_change=0;min_z=999;max_z=-999;pose_end=None
            for phase in [0,1/12,1/6,.25,.5,.75,1]:
                points,pose_end=sample(scene,rig,meshes,action,first+(last-first)*phase)
                assert all(math.isfinite(v) for p in points for v in p)
                max_change=max(max_change,max((p-q).length for p,q in zip(points,initial)))
                min_z=min(min_z,min(p.z for p in points));max_z=max(max_z,max(p.z for p in points))
            if clip not in ['Reference_A','Turn_Base','Aim_Base','Vehicle_Reference']:
                assert max_change>.0001,f'{clip} has no actual skin deformation'
            assert min_z>=-.005,f'{clip}: ground penetration {min_z} m'
            loop=clip in ['Walk','Run','Idle','Jump_Air','Hover_Idle','Cruise','Boost']
            seam=max(abs(v-w) for bone in pose0 for v,w in zip(pose0[bone],pose_end[bone])) if loop else None
            if loop:assert seam<.0001,f'{clip}: loop seam {seam}'
            result['clips'][clip]={'frames':[first,last],'max_vertex_motion_m':max_change,'min_z_m':min_z,'max_z_m':max_z,'loop_max_pose_error':seam}
        byname={a.name.split('.')[0]:a for a in actions}
        if 'Walk_To_Run' in byname:
            for label,a,ta,b,tb in [('walk_transition_entry','Walk_To_Run',0,'Walk',0),('walk_transition_exit','Walk_To_Run',1,'Run',0),('jump_takeoff','Jump_Start',1,'Jump_Air',0),('jump_landing','Jump_Air',1,'Jump_Land',0)]:
                aa=byname[a];bb=byname[b]
                _,pa=sample(scene,rig,meshes,aa,aa.frame_range[0]+(aa.frame_range[1]-aa.frame_range[0])*ta)
                _,pb=sample(scene,rig,meshes,bb,bb.frame_range[0]+(bb.frame_range[1]-bb.frame_range[0])*tb)
                error=max(abs(v-w) for bone in pa for v,w in zip(pa[bone],pb[bone]))
                result['transition_max_error'][label]=error
                assert error<.0002,f'{label} mismatch {error}'
        REPORT['files'][file]=result
    except Exception as exc:REPORT['errors'].append(file+': '+str(exc))
REPORT['all_checks_passed']=not REPORT['errors']
(ROOT/'Validation/roundtrip_report.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
print('ROUNDTRIP_RESULT',json.dumps({'files':len(REPORT['files']),'errors':REPORT['errors'],'all_checks_passed':REPORT['all_checks_passed']}))
assert REPORT['all_checks_passed'],REPORT['errors']
