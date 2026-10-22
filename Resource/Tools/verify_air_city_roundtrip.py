"""Blender reimport QA for every new aircraft and representative scenery assemblies.

Verifies bind-pose bounds, bone/slot preservation, real animation deformation,
loop seams, and inverse gear/canopy/hatch endpoint continuity after GLB export.
"""
import bpy
import json
import math
import traceback
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
recipes=json.loads((ROOT/'Documentation/aircraft_assemblies.json').read_text(encoding='utf-8'))['types']
scenery=json.loads((ROOT/'Documentation/scenery_assemblies.json').read_text(encoding='utf-8'))['assemblies']
report={'files':{},'errors':[]}


def points(meshes):
    deps=bpy.context.evaluated_depsgraph_get()
    result=[]
    for ob in sorted(meshes,key=lambda o:o.name):
        ev=ob.evaluated_get(deps)
        mesh=ev.to_mesh()
        result.extend(ev.matrix_world@v.co for v in mesh.vertices)
        ev.to_mesh_clear()
    return result


def sample(s,rig,meshes,action,phase):
    rig.animation_data.action=action
    if action.slots:
        rig.animation_data.action_slot=action.slots[0]
    a,b=action.frame_range
    frame=a+(b-a)*phase
    s.frame_set(int(frame),subframe=frame-int(frame))
    bpy.context.view_layer.update()
    return points(meshes)


for kind,info in recipes.items():
    for suffix,key in [('', 'default_parts'),('_Variant','alternate_parts')]:
        file=f'Export/Aircraft/{kind}{suffix}.glb'
        try:
            s=bpy.data.scenes.new('Reimport_'+kind+suffix)
            bpy.context.window.scene=s
            s.render.fps=30
            before=set(bpy.data.actions)
            before_objects=set(bpy.data.objects)
            bpy.ops.import_scene.gltf(filepath=str(ROOT/file))
            actions={a.name.split('.')[0]:a for a in set(bpy.data.actions)-before}
            imported=set(bpy.data.objects)-before_objects
            rigs=[o for o in imported if o.type=='ARMATURE']
            # Blender's importer creates an Icosphere in glTF_not_exported for
            # bone display. It is not an exported asset or a skinned part.
            meshes=[o for o in imported if o.type=='MESH' and
                    any(m.type=='ARMATURE' and m.object in rigs for m in o.modifiers)]
            assert len(rigs)==1 and len(meshes)==len(info[key]),dict(rigs=len(rigs),meshes=len(meshes),expected=len(info[key]))
            assert {o.get('asset_id') for o in meshes}==set(info[key]),'asset part IDs changed on import'
            rig=rigs[0]
            assert len(rig.data.bones)==17,('bone_count',len(rig.data.bones))
            assert {'driver_socket','passenger_socket','canopy','cargo','gear.L','gear.R'}<=set(rig.data.bones.keys())
            assert len(actions)==12,('clip_count',len(actions),list(actions))
            for t in rig.animation_data.nla_tracks:
                t.mute=True
            result={'mesh_parts':len(meshes),'bones':len(rig.data.bones),'clips':{},'endpoint_errors_m':{}}
            for clip,action in actions.items():
                p0=sample(s,rig,meshes,action,0)
                maximum=0
                for phase in [.125,.25,.5,.75,1]:
                    p=sample(s,rig,meshes,action,phase)
                    assert all(math.isfinite(x) for v in p for x in v)
                    maximum=max(maximum,max((a-b).length for a,b in zip(p0,p)))
                if clip!='Aircraft_Reference':
                    assert maximum>1e-4,clip+' does not deform exported meshes'
                seam=max((a-b).length for a,b in zip(p0,p))
                if clip in ['Flight_Idle','Cruise','Boost']:
                    assert seam<1e-4,(clip,seam)
                result['clips'][clip]={'max_vertex_motion_m':maximum,
                    'loop_seam_m':seam if clip in ['Flight_Idle','Cruise','Boost'] else None}
            for a,b in [('Canopy_Open','Canopy_Close'),('Cargo_Open','Cargo_Close'),('LandingGear_Retract','LandingGear_Deploy')]:
                for phase in [0,1]:
                    pa=sample(s,rig,meshes,actions[a],phase)
                    pb=sample(s,rig,meshes,actions[b],1-phase)
                    error=max((p-q).length for p,q in zip(pa,pb))
                    assert error<1e-4,(a,b,phase,error)
                    result['endpoint_errors_m'][a+'_'+str(phase)]=error
            report['files'][file]=result
        except Exception as exc:
            traceback.print_exc()
            report['errors'].append(file+': '+str(exc))

for name in ['Road_6Lane_4m','Road_Crossroad_12m','Airfield_LandingPad_16m',
             'City_TransitShelter','Ruin_FracturedArch','Nature_SailTree',
             'City_TrafficSignal','City_PedestrianHologram_6m']:
    file=f'Export/Scenery/{name}.glb'
    try:
        s=bpy.data.scenes.new('Reimport_'+name)
        bpy.context.window.scene=s
        before=set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=str(ROOT/file))
        meshes=[o for o in set(bpy.data.objects)-before if o.type=='MESH']
        assert {o.get('asset_id') for o in meshes}==set(scenery[name]['parts'])
        p=points(meshes)
        bounds=[[min(v[i] for v in p),max(v[i] for v in p)] for i in range(3)]
        assert bounds[2][0]>=-.201,(name,bounds)
        expected={'Road_6Lane_4m':(4,18),'Road_Crossroad_12m':(12,12),'Airfield_LandingPad_16m':(16,16)}.get(name)
        if expected:
            assert all(abs((bounds[i][1]-bounds[i][0])-expected[i])<1e-4 for i in [0,1])
        assert all(o.data.uv_layers and len(o.data.materials)>0 for o in meshes)
        if name=='City_TrafficSignal':
            assert bounds[2][0]>=4.7,'vehicle signal has ground support geometry'
            assert not any(any(word in o.get('asset_id','') for word in ['Pole','Foot','Pedestrian']) for o in meshes)
            assert len(meshes)==6,'hover housing, two serviceable stabilizers and three state glyphs required'
        if name=='City_PedestrianHologram_6m':
            assert bounds[2][1]<=.15,'pedestrian control must remain on the road surface'
        report['files'][file]={'mesh_parts':len(meshes),'bounds_m':bounds}
    except Exception as exc:
        report['errors'].append(file+': '+str(exc))

report['all_checks_passed']=not report['errors']
(ROOT/'Validation/air_city_roundtrip_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('AIR_CITY_ROUNDTRIP',json.dumps({'files_checked':len(report['files']),'errors':report['errors']}),flush=True)
assert report['all_checks_passed'],report['errors']
