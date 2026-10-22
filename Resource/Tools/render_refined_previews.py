"""Refresh saved preview scenes and enforce one mesh per legacy standalone part."""
import bpy
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'))
scene=bpy.data.scenes['00_Library'];bpy.context.window.scene=scene;scene.frame_set(0)
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
pairs=json.loads((ROOT/'Validation/refinement_pipeline_report.json').read_text(encoding='utf-8'))['glazing_companions']
for e in catalog['exports']:
    if e['type']!='module' or len(e['mesh_parts'])<2:continue
    frames=[n for n in e['mesh_parts'] if n in pairs]
    if len(frames)!=1:continue
    name=frames[0];obj=bpy.data.objects[name]
    for o in scene.objects:o.select_set(False)
    obj.select_set(True)
    rig=obj.parent if obj.parent and obj.parent.type=='ARMATURE' else None
    if rig:
        rig.select_set(True)
        for t in rig.animation_data.nla_tracks:t.mute=True
        rig.animation_data.action=None
        for b in rig.pose.bones:b.location=(0,0,0);b.rotation_quaternion=(1,0,0,0);b.scale=(1,1,1)
    p=ROOT/e['file'];stage=p.with_name(p.stem+'.staging.glb')
    bpy.ops.export_scene.gltf(filepath=str(stage),export_format='GLB',use_selection=True,use_active_scene=True,
        export_animations=False,export_skins=bool(rig),export_def_bones=False,
        export_yup=True,export_extras=True,export_cameras=False,export_lights=False,
        export_materials='EXPORT',export_apply=False,export_texcoords=True,export_normals=True)
    stage.replace(p);e['mesh_parts']=[name]
    print('STANDALONE_PART',name,flush=True)
p=ROOT/'Documentation/asset_catalog.json';temp=p.with_suffix('.json.new')
temp.write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');temp.replace(p)
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
vehicles=json.loads((ROOT/'Documentation/vehicle_assemblies.json').read_text(encoding='utf-8'))['types']
for kind,info in recipes['vehicles'].items():
    info['parts']=vehicles[kind]['default_parts'];info['alternate_parts']=vehicles[kind]['alternate_parts']
    info['glazing_companions']=vehicles[kind]['glazing_companions']
p=ROOT/'Documentation/assembly_recipes.json';temp=p.with_suffix('.json.new')
temp.write_text(json.dumps(recipes,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');temp.replace(p)
for scene_name,file in [('19_Glazing_Review','24_glazing_review'),('20_Architecture_Styles','25_architecture_styles'),
    ('23_Architecture_Closeups','26_architecture_closeups'),('21_Crosswalk_Separation','27_crosswalk_separation'),
    ('22_Legacy_Asset_Refinements','28_legacy_refinements'),('16_Transit_Connected_Example','20_transit_connected'),
    ('09_Vehicles','09_vehicles'),('09b_Vehicle_Variants','10_vehicle_variants'),
    ('10_Aircraft','12_aircraft'),('10b_Aircraft_Variants','13_aircraft_variants')]:
    s=bpy.data.scenes.get(scene_name)
    if not s:continue
    bpy.context.window.scene=s;s.render.filepath=str(ROOT/'Preview'/f'{file}.png')
    bpy.ops.render.render(write_still=True);print('REFRESHED_PREVIEW',file,flush=True)
print('REFINED_PREVIEWS_COMPLETE',flush=True)
