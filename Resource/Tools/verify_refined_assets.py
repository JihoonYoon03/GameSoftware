"""Validate delivered glazing pairs, real storeys, paint-only geometry and saved sources."""
import json
import sys
from pathlib import Path
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'Tools'))
from verify_resource_pack import read_glb,accessor
report=dict(source_pairs=0,glazing_assemblies=0,buildings={},paint_modules=0,opening_roundtrips={},errors=[])
data=json.loads((ROOT/'Validation/refinement_pipeline_report.json').read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'))
assert bpy.context.scene.name=='16_Transit_Connected_Example'
for row in data['parts']:
    raw=bpy.data.objects[row['source_object']];final=bpy.data.objects[row['id']]
    for ob,key in [(raw,'source_triangles'),(final,'optimized_triangles')]:
        ob.data.calc_loop_triangles();assert len(ob.data.loop_triangles)==row[key],row['id']
        assert ob.data.uv_layers
    assert final.data is not raw.data and row['optimized_triangles']<=500
    assert row['max_surface_error_m']<5e-5
    if final.parent and final.parent.type=='ARMATURE':
        for ob in [raw,final]:assert all(abs(sum(x.weight for x in v.groups)-1)<1e-6 for v in ob.data.vertices),ob.name
    report['source_pairs']+=1

for family,file in [('Vehicles','vehicle_assemblies'),('Aircraft','aircraft_assemblies')]:
    meta=json.loads((ROOT/f'Documentation/{file}.json').read_text(encoding='utf-8'))
    for kind,info in meta['types'].items():
        for suffix,parts in [('',info['default_parts']),('_Variant',info['alternate_parts'])]:
            doc,buf=read_glb(ROOT/f'Export/{family}/{kind}{suffix}.glb')
            nodes={n.get('extras',{}).get('asset_id'):n for n in doc['nodes'] if 'mesh' in n}
            for frame,glass in info['glazing_companions'].items():
                if frame not in parts:continue
                assert glass in parts and frame in nodes and glass in nodes
                for name in [frame,glass]:
                    node=nodes[name];skin=doc['skins'][node['skin']]
                    bone_names=[doc['nodes'][i]['name'] for i in skin['joints']]
                    for prim in doc['meshes'][node['mesh']]['primitives']:
                        aa=prim['attributes'];jj=accessor(doc,buf,aa['JOINTS_0']);ww=accessor(doc,buf,aa['WEIGHTS_0'])
                        expected='chassis' if '_Payload_' in frame else 'canopy'
                        assert all(bone_names[j]==expected for joints,weights in zip(jj,ww) for j,w in zip(joints,weights) if w>0),name
                material_names=[doc['materials'][p['material']]['name'] for p in doc['meshes'][nodes[glass]['mesh']]['primitives']]
                assert any(n.startswith('Glazing_') for n in material_names)
                assert any(m.get('alphaMode')=='BLEND' for m in doc['materials'] if m['name'].startswith('Glazing_'))
                report['glazing_assemblies']+=1

styles=json.loads((ROOT/'Documentation/architecture_styles.json').read_text(encoding='utf-8'))['styles']
assert len(styles)==5 and sum(i['floor_count']>=10 for i in styles.values())>=3
for name,info in styles.items():
    doc,buf=read_glb(ROOT/f'Export/Buildings/{name}.glb')
    root=next(n for n in doc['nodes'] if n.get('extras',{}).get('floor_count'))
    assert root['extras']['floor_count']==info['floor_count']
    slabs=[n for n in doc['nodes'] if n.get('extras',{}).get('asset_id')=='Arch_Floor_Slab_Unit']
    levels={int(n['extras']['floor_index']) for n in slabs}
    assert levels==set(range(1,info['floor_count']+1)),(name,levels)
    for n in slabs:
        z=n.get('translation',[0,0,0])[1]
        assert abs(z-(.64+(n['extras']['floor_index']-1)*3.6))<1e-4,(name,n)
    assert any(n.get('extras',{}).get('asset_id')=='Arch_Facade_Glazing_3m' for n in doc['nodes'])
    # Roundtrip all architectural assemblies and measure actual world-space height.
    s=bpy.data.scenes.new('BUILDING_REIMPORT');bpy.context.window.scene=s
    bpy.ops.import_scene.gltf(filepath=str(ROOT/f'Export/Buildings/{name}.glb'))
    bpy.context.view_layer.update()
    points=[ob.matrix_world@v.co for ob in s.objects if ob.type=='MESH' for v in ob.data.vertices]
    height=max(p.z for p in points)-min(p.z for p in points)
    assert height>info['floor_count']*3.6,(name,height)
    report['buildings'][name]=dict(floors=info['floor_count'],measured_height_m=height,storey_levels_checked=len(levels))
    for o in list(s.objects):bpy.data.objects.remove(o,do_unlink=True)
    bpy.data.scenes.remove(s)

for lanes in [2,4,6]:
    doc,buf=read_glb(ROOT/f'Export/Transit/Transit_CrosswalkPaint_{lanes}L.glb')
    assert len(doc['meshes'])==1
    for p in doc['meshes'][0]['primitives']:
        assert doc['materials'][p['material']]['name']=='Ceramic'
        points=accessor(doc,buf,p['attributes']['POSITION'])
        assert min(v[1] for v in points)>.008 and max(v[1] for v in points)<.020
    report['paint_modules']+=1
layout=json.loads((ROOT/'Documentation/transit_layout_example.json').read_text(encoding='utf-8'))
crossing=next((i for i,p in enumerate(layout['placements']) if p.get('module')=='Transit_Road_2L_Crosswalk_4m'),None)
# The connected example's explicit crossing branch must use the 2m independent walks.
assert sum(p['module']=='Transit_Walk_W4_Straight_2m' for p in layout['placements'])>=2

for family,kind in [('Vehicles','Coupe'),('Aircraft','Warden_Survey')]:
    s=bpy.data.scenes.new('GLAZING_REIMPORT');bpy.context.window.scene=s
    before=set(bpy.data.actions)
    bpy.ops.import_scene.gltf(filepath=str(ROOT/f'Export/{family}/{kind}.glb'))
    rig=next(o for o in s.objects if o.type=='ARMATURE')
    actions=list(set(bpy.data.actions)-before)
    action=next(a for a in actions if a.name.split('.')[0]=='Canopy_Open')
    for t in rig.animation_data.nla_tracks:t.mute=True
    rig.animation_data.action=action
    panes=[o for o in s.objects if o.type=='MESH' and o.get('asset_id','').endswith('_Glazing')]
    assert panes
    def sample(frame):
        s.frame_set(int(frame));bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();pts=[]
        for o in panes:
            e=o.evaluated_get(dg);mesh=e.to_mesh();pts.extend(e.matrix_world@v.co for v in mesh.vertices);e.to_mesh_clear()
        return pts
    a,b=sample(action.frame_range[0]),sample(action.frame_range[1])
    motion=max((p-q).length for p,q in zip(a,b));assert motion>.1
    report['opening_roundtrips'][kind]=dict(glazing_meshes=len(panes),max_glazing_motion_m=motion)
    for o in list(s.objects):bpy.data.objects.remove(o,do_unlink=True)
    bpy.data.scenes.remove(s)
report['all_checks_passed']=True
(ROOT/'Validation/refined_assets_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('REFINED_ASSETS_CHECKED',json.dumps(report),flush=True)
